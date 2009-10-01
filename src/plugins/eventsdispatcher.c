/*
Copyright (C) 2006  Adam Charrett

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA

eventsdispatcher.c

Plugin to allow access to internal event information.

*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <pthread.h>
#include <sys/time.h>

#include "main.h"
#include "logging.h"
#include "plugin.h"
#include "events.h"
#include "commands.h"
#include "objects.h"
#include "deferredproc.h"
#include "list.h"
#include "deliverymethod.h"
#include "properties.h"

/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/
typedef struct ConsoleContext_s {
    CommandContext_t *context;
    pthread_mutex_t printMutex;
}ConsoleContext_t;

typedef struct EventDescription_s {
    struct timeval at;
    char *description;
}EventDescription_t;

typedef struct EventDispatcherListener_s {
    char *name;
    bool allEvents;
    List_t *events;

    bool console;
    union {
        ConsoleContext_t *console;
        DeliveryMethodInstance_t *dmInstance;
    }arg;
}EventDispatcherListener_t;

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void EventDispatcherInstalled(bool installed);
static void CommandListen(int argc, char **argv);
static void CommandAddListener(int argc, char **argv);
static void CommandRemoveListener(int argc, char **argv);
static void CommandListListeners(int argc, char **argv);

static void CommandAddListenEvent(int argc, char **argv);
static void CommandRemoveListenEvent(int argc, char **argv);
static void CommandListListenEvents(int argc, char **argv);

static void EventCallback(void *arg, Event_t event, void *payload);
static void DeferredInformListeners(void *arg);

static void ConsoleContextEventPrint(ConsoleContext_t *context, ...);

static void EventDescriptionDestructor(void *arg);
static void EventDispatcherListenerDestructor(void *arg);

static void AddListener(EventDispatcherListener_t *listener);
static void RemoveListener(EventDispatcherListener_t *listener);
static EventDispatcherListener_t *FindListener(char *name);
static void AddListenerEvent(EventDispatcherListener_t *listener, char *filter);
static bool RemoveListenerEvent(EventDispatcherListener_t *listener, char *filter);
static int ListenerPropertyTableGet(void *userArg, int row, int column, PropertyValue_t *value);
static int ListenerPropertyTableCount(void *userArg);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static List_t *listenersList;
static pthread_mutex_t listenersMutex = PTHREAD_MUTEX_INITIALIZER;
static const char console[] = "<Console>";
static const char EVENTDISPATCH[] = "EventDispatch";
static const char propertiesParentPath[] = "commands.eventlisteners";
static PropertyTableDescription_t tableDescription = {
    .nrofColumns = 1,
    .columns[0] = {"Event", "Events the listener is registered to received", PropertyType_String}
};

/*******************************************************************************
* Plugin Setup                                                                 *
*******************************************************************************/
PLUGIN_COMMANDS(
    {
        "listen",
        0, 0,
        "Listen for events.",
        "listen\n"
        "Listen for internal events, to add events to listen to, type\n"
        "+<SourceName>.<EventName>\n"
        "To stop listening for an event, type\n"
        "-<SourceName>.<EventName>\n"
        "To find out what events are being listened to, type\n"
        "=\n"
        "To end listening to events type\n"
        "e\n"
        "NOTE: Console only! For remote connections use addlistener",
        CommandListen
    },
    {
        "addlistener",
        2, 2,
        "Add a destination to send event notification to.",
        "addlistener <name> <MRL>\n"
        "Add an MRL destination to send event notifications to.\n"
        "The MRL can be any delivery system that supports sending opaque chunks,"
        " udp and file are 2 examples.",
        CommandAddListener
    },
    {
        "rmlistener",
        1, 1,
        "Remove a destination to send event notification to.",
        "rmlistener <name>\n"
        "Remove a destination to send event notifications over udp to.",
        CommandRemoveListener
    },
    {
        "lslisteners",
        0, 0,
        "List all registered event listener",
        "List all registered UDP event listener",
        CommandListListeners
    },
    {
        "addlistenevent",
        2, 2,
        "Add an internal event to monitor.",
        "addlistenevent <name> <event>\n"
        "Add an internal event (<event>) to monitor to the listener specified by <name>.\n"
        "<event> can be either a full event name, an event source or the special name \"<all>\"",
        CommandAddListenEvent
    },
    {
        "rmlistenevent",
        2, 2,
        "Remove an internal event to monitor",
        "rmlistenevent <name> <event>\n"
        "Remove an internal event previously monitored by a call to addevent.",
        CommandRemoveListenEvent
    },
    {
        "lslistenevents",
        1, 1,
        "List all registered event listener",
        "List all registered UDP event listener",
        CommandListListenEvents
    }
);

PLUGIN_FEATURES(
    PLUGIN_FEATURE_INSTALL(EventDispatcherInstalled)
    );

PLUGIN_INTERFACE_CF(
    PLUGIN_FOR_ALL,
    "EventsDispatcher",
    "0.1",
    "Interface to internal events.",
    "charrea6@users.sourceforge.net"
);

static void EventDispatcherInstalled(bool installed)
{
    if (installed)
    {
        ObjectRegisterTypeDestructor(EventDescription_t, EventDescriptionDestructor);
        ObjectRegisterTypeDestructor(EventDispatcherListener_t, EventDispatcherListenerDestructor);
        listenersList = ListCreate();
        PropertiesAddProperty("commands", "eventlisteners", "", PropertyType_None, NULL, NULL, NULL);
    }
    else
    {
        ListIterator_t iterator;
        PropertiesRemoveAllProperties(propertiesParentPath);
        EventsUnregisterListener(EventCallback, NULL);
        for (ListIterator_Init(iterator, listenersList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
        {
            EventDispatcherListener_t *listener = ListIterator_Current(iterator);
            listener->arg.dmInstance = NULL; /* Delivery Method Manager will already have destroyed this by the time we get here! */
        }
        ObjectListFree(listenersList);
    }
}

/*******************************************************************************
* Command Functions                                                              *
*******************************************************************************/
static void CommandListen(int argc, char **argv)
{
    bool quit=FALSE;
    char buffer[256];
    char *eventStr;
    ConsoleContext_t context;
    CommandContext_t *cmdContext = CommandContextGet();
    EventDispatcherListener_t *listener;
    if (cmdContext->remote)
    {
        CommandError(COMMAND_ERROR_GENERIC, "Only supported from the console!");
        return;
    }

    context.context = CommandContextGet();
    pthread_mutex_init(&context.printMutex, NULL);

    listener = ObjectCreateType(EventDispatcherListener_t);
    listener->name = strdup((char *)console);
    listener->events = ListCreate();
    listener->console = TRUE;
    listener->arg.console = &context;

    CommandPrintf("Listening\n");
    AddListener(listener);


    while(!quit && !ExitProgram)
    {
        if (CommandGets(buffer, sizeof(buffer) - 1))
        {
            char *nl = strchr(buffer, '\n');
            if (nl)
            {
                *nl = 0;
            }
            nl = strchr(buffer, '\r');
            if (nl)
            {
                *nl = 0;
            }
            pthread_mutex_lock(&context.printMutex);
            switch(buffer[0])
            {
                case '+':
                    /* Register for events */
                    if (buffer[1] == 0)
                    {
                        eventStr = "<all>";
                    }
                    else
                    {
                        eventStr = buffer + 1;
                    }
                    AddListenerEvent(listener, eventStr);
                    CommandPrintf("+%s\n", eventStr);
                    break;
                case '-':
                    /* Unregister an event */
                    if (buffer[1] == 0)
                    {
                        eventStr = "<all>";
                    }
                    else
                    {
                        eventStr = buffer + 1;
                    }
                    if (RemoveListenerEvent(listener, eventStr))
                    {
                        CommandPrintf("-%s\n", eventStr);
                    }
                    break;
                case '=':
                    /* Display registered events */
                    if (listener->allEvents)
                    {
                        CommandPrintf("=<All>\n");
                    }
                    else
                    {
                        ListIterator_t iterator;
                        for (ListIterator_Init(iterator, listener->events);
                             ListIterator_MoreEntries(iterator);
                             ListIterator_Next(iterator))
                        {
                            char *eventStr = ListIterator_Current(iterator);
                            CommandPrintf("=%s\n", eventStr);
                        }
                    }
                    break;
                case 'e':
                    quit=TRUE;
                    break;
            }
            pthread_mutex_unlock(&context.printMutex);
        }
    }

    RemoveListener(listener);
    ObjectRefDec(listener);
    pthread_mutex_destroy(&context.printMutex);
}

static void CommandAddListener(int argc, char **argv)
{
    EventDispatcherListener_t *listener;
    DeliveryMethodInstance_t *mrlInstance;

    listener = FindListener(argv[0]);
    if (listener)
    {
        CommandError(COMMAND_ERROR_GENERIC, "Listener already exists!");
        return;
    }

    mrlInstance = DeliveryMethodCreate(argv[1]);
    if (!mrlInstance)
    {
        CommandError(COMMAND_ERROR_GENERIC, "Invalid MRL!");
        return;
    }

    listener = ObjectCreateType(EventDispatcherListener_t);
    listener->name = strdup(argv[0]);
    listener->events = ListCreate();
    listener->console = FALSE;
    listener->arg.dmInstance = mrlInstance;

    AddListener(listener);
    PropertiesAddTableProperty(propertiesParentPath, argv[0], "Event Listener", &tableDescription, listener,
        ListenerPropertyTableGet, NULL, ListenerPropertyTableCount);

}

static void CommandRemoveListener(int argc, char **argv)
{
    EventDispatcherListener_t *listener;
    listener = FindListener(argv[0]);
    if (!listener)
    {
        CommandError(COMMAND_ERROR_GENERIC, "Listener not found!");
        return;
    }

    RemoveListener(listener);
    ObjectRefDec(listener);
    PropertiesRemoveProperty(propertiesParentPath, argv[1]);
}

static void CommandListListeners(int argc, char **argv)
{
    ListIterator_t iterator;

    pthread_mutex_lock(&listenersMutex);
    for (ListIterator_Init(iterator, listenersList);
         ListIterator_MoreEntries(iterator);
         ListIterator_Next(iterator))
    {
        EventDispatcherListener_t *listener = (EventDispatcherListener_t *)ListIterator_Current(iterator);
        CommandPrintf("%s : %s\n", listener->name, listener->console ? console:listener->arg.dmInstance->mrl);
    }
    pthread_mutex_unlock(&listenersMutex);
}

static void CommandAddListenEvent(int argc, char **argv)
{
    EventDispatcherListener_t *listener;
    listener = FindListener(argv[0]);
    if (!listener)
    {
        CommandError(COMMAND_ERROR_GENERIC, "Listener not found!");
        return;
    }
    AddListenerEvent(listener, argv[1]);
}

static void CommandRemoveListenEvent(int argc, char **argv)
{
    EventDispatcherListener_t *listener;
    listener = FindListener(argv[0]);
    if (!listener)
    {
        CommandError(COMMAND_ERROR_GENERIC, "Listener not found!");
        return;
    }
    if (!RemoveListenerEvent(listener, argv[1]))
    {
        CommandError(COMMAND_ERROR_GENERIC, "Event not found!");
        return;
    }
    ObjectRefDec(listener);
}

static void CommandListListenEvents(int argc, char **argv)
{
    EventDispatcherListener_t *listener;
    ListIterator_t iterator;

    listener = FindListener(argv[0]);
    if (!listener)
    {
        CommandError(COMMAND_ERROR_GENERIC, "Listener not found!");
        return;
    }

    for (ListIterator_Init(iterator, listener->events);
         ListIterator_MoreEntries(iterator);
         ListIterator_Next(iterator))
    {
        char *eventStr = ListIterator_Current(iterator);
        CommandPrintf("%s\n", eventStr);
    }
}
/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/
static void EventCallback(void *arg, Event_t event, void *payload)
{
    char *desc = EventsEventToString(event, payload);
    EventDescription_t *eventDesc = ObjectCreateType(EventDescription_t);
    gettimeofday(&eventDesc->at, NULL);
    eventDesc->description = desc;
    DeferredProcessingAddJob(DeferredInformListeners, eventDesc);
    ObjectRefDec(eventDesc);
}

static void DeferredInformListeners(void * arg)
{
    EventDescription_t *eventDesc = arg;
    ListIterator_t iterator;
    char *outputLine = NULL;
    size_t outputLineLen = 0;
    DVBAdapter_t *adapter = MainDVBAdapterGet();

    LogModule(LOG_DEBUG, EVENTDISPATCH, "Processing event (%ld.%ld) %s\n",
        eventDesc->at.tv_sec, eventDesc->at.tv_usec, eventDesc->description);
    pthread_mutex_lock(&listenersMutex);

    for (ListIterator_Init(iterator, listenersList);
         ListIterator_MoreEntries(iterator);
         ListIterator_Next(iterator))
    {
        EventDispatcherListener_t *listener = (EventDispatcherListener_t *)ListIterator_Current(iterator);
        bool inform = FALSE;
        LogModule(LOG_DEBUG, EVENTDISPATCH, "Checking listener %s\n", listener->name);
        if (listener->allEvents)
        {
            inform = TRUE;
        }
        else
        {
            ListIterator_t eventFilterIterator;
            for (ListIterator_Init(eventFilterIterator, listener->events);
                 ListIterator_MoreEntries(eventFilterIterator);
                 ListIterator_Next(eventFilterIterator))
            {
                char *eventFilter = (char *)ListIterator_Current(eventFilterIterator);
                if (strncmp(eventFilter, eventDesc->description, strlen(eventFilter)) == 0)
                {
                    inform = TRUE;
                    break;
                }
            }
        }
        if (inform)
        {
            LogModule(LOG_DEBUG, EVENTDISPATCH, "Informing listener %s\n", listener->name);
            if (listener->console)
            {
                ConsoleContextEventPrint(listener->arg.console, eventDesc->description);
            }
            else
            {
                if (outputLine == NULL)
                {
                    struct tm *localtm = localtime(&eventDesc->at.tv_sec);
                    char timeStr[21]; /* xxxx-xx-xx xx:xx:xx */
                    strftime(timeStr, sizeof(timeStr)-1, "%F %T", localtm);
                    outputLineLen = asprintf(&outputLine, "%s.%ld %d %s\n",
                        timeStr, eventDesc->at.tv_usec, adapter->adapter,
                        eventDesc->description);
                }
                if (outputLine)
                {
                    DeliveryMethodOutputBlock(listener->arg.dmInstance, outputLine, outputLineLen);
                }
            }
        }
    }

    pthread_mutex_unlock(&listenersMutex);
    ObjectRefDec(eventDesc);
}

static void ConsoleContextEventPrint(ConsoleContext_t *context, ...)
{
    va_list args;
    pthread_mutex_lock(&context->printMutex);
    va_start(args, context);
    vfprintf(context->context->outfp, "!%s\n", args);
    va_end(args);
    pthread_mutex_unlock(&context->printMutex);
}

static void EventDescriptionDestructor(void *arg)
{
    EventDescription_t *desc = arg;
    free(desc->description);
}

static void EventDispatcherListenerDestructor(void *arg)
{
    EventDispatcherListener_t *listener = arg;
    ListFree(listener->events, free);
    free(listener->name);
    if (!listener->console)
    {
        if (listener->arg.dmInstance)
        {
            DeliveryMethodDestroy(listener->arg.dmInstance);
        }
    }
}

static void AddListener(EventDispatcherListener_t *listener)
{
    pthread_mutex_lock(&listenersMutex);
    ListAdd(listenersList, listener);
    if (ListCount(listenersList) == 1)
    {
        LogModule(LOG_DEBUG, EVENTDISPATCH, "Adding Event callback\n");
        EventsRegisterListener(EventCallback, NULL);
    }
    pthread_mutex_unlock(&listenersMutex);
}

static void RemoveListener(EventDispatcherListener_t *listener)
{
    pthread_mutex_lock(&listenersMutex);
    ListRemove(listenersList, listener);
    if (ListCount(listenersList) == 0)
    {
        LogModule(LOG_DEBUG, EVENTDISPATCH, "Removing Event callback\n");
        EventsUnregisterListener(EventCallback, NULL);
        LogModule(LOG_DEBUG, EVENTDISPATCH, "Removed Event callback\n");
    }
    pthread_mutex_unlock(&listenersMutex);
}

static EventDispatcherListener_t *FindListener(char *name)
{
    ListIterator_t iterator;
    EventDispatcherListener_t *result = NULL;
    pthread_mutex_lock(&listenersMutex);
    for (ListIterator_Init(iterator, listenersList);
         ListIterator_MoreEntries(iterator);
         ListIterator_Next(iterator))
    {
        EventDispatcherListener_t *listener = (EventDispatcherListener_t*) ListIterator_Current(iterator);
        if (strcmp(listener->name, name) == 0)
        {
            result = listener;
            break;
        }
    }
    pthread_mutex_unlock(&listenersMutex);
    return result;
}

static void AddListenerEvent(EventDispatcherListener_t *listener, char *filter)
{
    pthread_mutex_lock(&listenersMutex);
    if (strcmp(filter, "<all>") == 0)
    {
        listener->allEvents = TRUE;
    }
    else
    {
        ListAdd(listener->events, strdup(filter));
    }
    pthread_mutex_unlock(&listenersMutex);
}

static bool RemoveListenerEvent(EventDispatcherListener_t *listener, char *filter)
{
    bool found = FALSE;
    pthread_mutex_lock(&listenersMutex);
    if (strcmp(filter, "<all>") == 0)
    {
        listener->allEvents = FALSE;
        found = TRUE;
    }
    else
    {
        ListIterator_t iterator;

        for (ListIterator_Init(iterator, listener->events);
             ListIterator_MoreEntries(iterator);
             ListIterator_Next(iterator))
        {
            char *eventStr = ListIterator_Current(iterator);
            if (strcmp(eventStr, filter) == 0)
            {
                ListRemoveCurrent(&iterator);
                free(eventStr);
                found = TRUE;
                break;
            }
        }
    }
    pthread_mutex_unlock(&listenersMutex);
    return found;
}

static int ListenerPropertyTableGet(void *userArg, int row, int column, PropertyValue_t *value)
{
    EventDispatcherListener_t *listener = userArg;
    int count = ListenerPropertyTableCount(userArg);
    int i = 0;
    ListIterator_t iterator;
    if (row > count)
    {
        return -1;
    }
    value->u.string = NULL;
    if (listener->allEvents)
    {
        if (row == 0)
        {
            value->u.string = strdup("*");
        }
        else
        {
            i = 1;
        }
    }
    if (value->u.string == NULL)
    {

        for (ListIterator_Init(iterator, listener->events);
             ListIterator_MoreEntries(iterator);
             ListIterator_Next(iterator))
        {
            if (i == row)
            {
                char *eventStr = ListIterator_Current(iterator);
                value->u.string = strdup(eventStr);
                break;
            }
            i ++;
        }
    }
    return 0;
}

static int ListenerPropertyTableCount(void *userArg)
{
    EventDispatcherListener_t *listener = userArg;
    int count = 0;
    if (listener->allEvents)
    {
        count ++;
    }
    count = ListCount(listener->events);
    return count;
}
