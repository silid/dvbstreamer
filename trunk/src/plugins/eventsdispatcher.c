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

#include "main.h"
#include "logging.h"
#include "plugin.h"
#include "events.h"
#include "commands.h"
#include "objects.h"
#include "deferredproc.h"
/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/
typedef struct ListeningContext_s {
    bool active;
    CommandContext_t *context;
    bool allEvents;
    List_t *events;
    pthread_mutex_t printMutex;
}ListeningContext_t;

typedef struct EventDescription_s {
    ListeningContext_t *context;
    char *description;
}EventDescription_t;
/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void CommandListen(int argc, char **argv);
static void EventCallback(void *arg, Event_t event, void *payload);
static void DeferredEventPrint(void *arg);
static void ContextEventPrint(CommandContext_t *context, ... );
static void ListeningContextDestructor(void *arg);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
#ifdef __CYGWIN__
#define PluginInterface EventsDispatcherPluginInterface
#endif
/*******************************************************************************
* Plugin Setup                                                                 *
*******************************************************************************/
PLUGIN_COMMANDS(
    {
        "listen",
        TRUE, 0, 0,
        "Listen for events.",
        "listen\n"
        "Listen for internal events, to add events to listen to, type\n"
        "+<SourceName>.<EventName>\n"
        "To stop listening for an event, type\n"
        "-<SourceName>.<EventName>\n"
        "To find out what events are being listened to, type\n"
        "=\n"
        "To end listening to events type\n"
        "e",
        CommandListen
    }
);

PLUGIN_INTERFACE_C(
    PLUGIN_FOR_ALL,
    "EventsDispatcher", 
    "0.1", 
    "Interface to internal events.", 
    "charrea6@users.sourceforge.net"
);

static void CommandListen(int argc, char **argv)
{
    bool quit=FALSE;
    char buffer[256];
    ListeningContext_t *listeningContext;

    ObjectRegisterType(EventDescription_t);
    ObjectRegisterTypeDestructor(ListeningContext_t, ListeningContextDestructor);
    listeningContext = ObjectCreateType(ListeningContext_t);
    listeningContext->active = TRUE;
    listeningContext->events = ListCreate();
    listeningContext->context = CommandContextGet();
    pthread_mutex_init(&listeningContext->printMutex, NULL);

    
    CommandPrintf("Listening\n");
    EventsRegisterListener(EventCallback, listeningContext);    

    
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
            pthread_mutex_lock(&listeningContext->printMutex);
            switch(buffer[0])
            {
                case '+':
                    /* Register for events */
                    if (buffer[1] == 0)
                    {
                        if (!listeningContext->allEvents)
                        {
                            CommandPrintf("+<<All>>\n");                            
                            listeningContext->allEvents = TRUE;
                        }
                    }
                    else
                    {
                        char *eventStr = strdup(buffer + 1);
                        CommandPrintf("+%s\n", eventStr);
                        ListAdd(listeningContext->events, eventStr);
                    }
                    break;
                case '-':
                    /* Unregister an event */
                    if (buffer[1] == 0)
                    {
                        if (listeningContext->allEvents)
                        {
                            CommandPrintf("-<<All>>\n");
                            listeningContext->allEvents = FALSE;
                        }
                    }
                    else
                    {
                        ListIterator_t iterator;
                        for (ListIterator_Init(iterator, listeningContext->events);
                             ListIterator_MoreEntries(iterator);
                             ListIterator_Next(iterator))
                        {
                            char *eventStr = ListIterator_Current(iterator);   
                            if (strcmp(eventStr, buffer + 1) == 0)
                            {
                                CommandPrintf("-%s\n", eventStr);
                                ListRemoveCurrent(&iterator);
                                free(eventStr);
                                break;
                            }
                        }
                    }
                    break;
                case '=':
                    /* Display registered events */
                    if (listeningContext->allEvents)
                    {
                        CommandPrintf("=<<All>>\n");
                    }
                    else
                    {
                        ListIterator_t iterator;
                        for (ListIterator_Init(iterator, listeningContext->events);
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
            pthread_mutex_unlock(&listeningContext->printMutex);            
        }
    }

    EventsUnregisterListener(EventCallback, listeningContext);
    listeningContext->active=FALSE;
    ObjectRefDec(listeningContext);   
}

static void EventCallback(void *arg, Event_t event, void *payload)
{
    ListeningContext_t *listeningContext = arg;
    if (listeningContext->active)
    {
        ListIterator_t iterator;
        char *desc = EventsEventToString(event, payload);
        bool found=FALSE;
        if (listeningContext->allEvents)
        {
            found = TRUE;
        }
        else
        {
            for (ListIterator_Init(iterator, listeningContext->events);
                 ListIterator_MoreEntries(iterator);
                 ListIterator_Next(iterator))
            {
                char *eventStr = ListIterator_Current(iterator);
                if (strncmp(eventStr, desc, strlen(eventStr)) == 0)
                {
                    found = TRUE;
                    break;
                }
            }
        }
        if (found)
        {
            EventDescription_t *eventDesc = ObjectCreateType(EventDescription_t);
            if (eventDesc)
            {
                eventDesc->description = desc;
                ObjectRefInc(listeningContext);
                eventDesc->context = listeningContext;
                DeferredProcessingAddJob(DeferredEventPrint,eventDesc);
                ObjectRefDec(eventDesc);
            }
        }
        else
        {
            free(desc);
        }
    }
}

static void DeferredEventPrint(void * arg)
{
    EventDescription_t *eventDesc = arg;
    ListeningContext_t *listeningContext = eventDesc->context;
    if (listeningContext->active)
    {
        pthread_mutex_lock(&listeningContext->printMutex);    
        ContextEventPrint(eventDesc->context->context, eventDesc->description);
        free(eventDesc->description);
        pthread_mutex_unlock(&listeningContext->printMutex);
    }
    ObjectRefDec(eventDesc);
    ObjectRefDec(listeningContext);
}

static void ContextEventPrint(CommandContext_t *context, ...)
{
    va_list args;    
    va_start(args, context);
    context->printf(context, "!%s\n", args);
    va_end(args);
}

static void ListeningContextDestructor(void *arg)
{
    ListeningContext_t *context = arg;
    ListFree(context->events, free);
    pthread_mutex_destroy(&context->printMutex);
}
