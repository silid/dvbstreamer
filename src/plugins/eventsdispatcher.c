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
typedef struct EventDescription_s {
    struct timeval at;
    char *eventName;
    char *description;
}EventDescription_t;

typedef struct EventDispatcherListener_s {
    char *name;
    bool allEvents;
    List_t *events;
    DeliveryMethodInstance_t *dmInstance;

}EventDispatcherListener_t;

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void EventDispatcherInstalled(bool installed);
static void CommandAddListener(int argc, char **argv);
static void CommandRemoveListener(int argc, char **argv);
static void CommandListListeners(int argc, char **argv);

static void CommandAddListenEvent(int argc, char **argv);
static void CommandRemoveListenEvent(int argc, char **argv);
static void CommandListListenEvents(int argc, char **argv);

static void EventCallback(void *arg, Event_t event, void *payload);
static void DeferredInformListeners(void *arg);

static void EventDescriptionDestructor(void *arg);
static void EventDispatcherListenerDestructor(void *arg);

static void AddListener(EventDispatcherListener_t *listener);
static void RemoveListener(EventDispatcherListener_t *listener);
static EventDispatcherListener_t *FindListener(char *name);
static void AddListenerEvent(EventDispatcherListener_t *listener, char *filter);
static bool RemoveListenerEvent(EventDispatcherListener_t *listener, char *filter);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static List_t *listenersList;
static pthread_mutex_t listenersMutex = PTHREAD_MUTEX_INITIALIZER;
static const char EVENTDISPATCH[] = "EventDispatch";

/*******************************************************************************
* Plugin Setup                                                                 *
*******************************************************************************/
PLUGIN_COMMANDS(
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
    }
    else
    {
        ListIterator_t iterator;
        EventsUnregisterListener(EventCallback, NULL);
        for (ListIterator_Init(iterator, listenersList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
        {
            EventDispatcherListener_t *listener = ListIterator_Current(iterator);
            listener->dmInstance = NULL; /* Delivery Method Manager will already have destroyed this by the time we get here! */
        }
        ObjectListFree(listenersList);
    }
}

/*******************************************************************************
* Command Functions                                                            *
*******************************************************************************/
static void CommandAddListener(int argc, char **argv)
{
    EventDispatcherListener_t *listener;
    DeliveryMethodInstance_t *mrlInstance;

    listener = FindListener(argv[0]);
    if (listener)
    {
        ObjectRefDec(listener);
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
    listener->dmInstance = mrlInstance;

    AddListener(listener);
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
    ObjectRefDec(listener); /* Do this twice as FindListener increments the ref count */
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
        CommandPrintf("%s : %s\n", listener->name, listener->dmInstance->mrl);
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
    ObjectRefDec(listener);
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
    ObjectRefDec(listener);
}
/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/
static void EventCallback(void *arg, Event_t event, void *payload)
{
    char *desc = EventsEventToString(event, payload);
    char *eventName = EventsEventName(event);
    EventDescription_t *eventDesc = ObjectCreateType(EventDescription_t);
    gettimeofday(&eventDesc->at, NULL);
    eventDesc->eventName = eventName;
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
                if (strncmp(eventFilter, eventDesc->eventName, strlen(eventFilter)) == 0)
                {
                    inform = TRUE;
                    break;
                }
            }
        }
        if (inform)
        {
            LogModule(LOG_DEBUG, EVENTDISPATCH, "Informing listener %s\n", listener->name);
            if (outputLine == NULL)
            {
                struct tm *localtm = localtime(&eventDesc->at.tv_sec);
                char timeStr[21]; /* xxxx-xx-xx xx:xx:xx */
                PropertyValue_t value;
                PropertiesGet("adapter.number", &value);
                strftime(timeStr, sizeof(timeStr)-1, "%F %T", localtm);
                outputLineLen = asprintf(&outputLine, "---\n"
                                                      "Time: %s.%ld\n"
                                                      "Adapter: %d\n"
                                                      "%s...\n",
                    timeStr, eventDesc->at.tv_usec, value.u.integer,
                    eventDesc->description);
            }
            if (outputLine)
            {
                DeliveryMethodOutputBlock(listener->dmInstance, outputLine, outputLineLen);
            }
        }
    }

    pthread_mutex_unlock(&listenersMutex);
    ObjectRefDec(eventDesc);
}

static void EventDescriptionDestructor(void *arg)
{
    EventDescription_t *desc = arg;
    free(desc->eventName);
    free(desc->description);
}

static void EventDispatcherListenerDestructor(void *arg)
{
    EventDispatcherListener_t *listener = arg;
    ListFree(listener->events, free);
    free(listener->name);
    if (listener->dmInstance)
    {
        DeliveryMethodDestroy(listener->dmInstance);
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
            ObjectRefInc(result);
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

