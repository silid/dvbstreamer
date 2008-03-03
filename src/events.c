/*
Copyright (C) 2008  Adam Charrett

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

event.c

Event Management functions, for creating and firing events to listeners.

*/

#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include "objects.h"
#include "list.h"
#include "events.h"
#include "logging.h"

/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/
/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/

struct EventSource_s
{
    char *name;
    List_t *events;
    List_t *listeners;
};

struct Event_s
{
    EventSource_t source;
    char *name;
    List_t *listeners;
    EventToString_t toString;
};

typedef struct EventListenerDetails_s
{
    EventListener_t callback;
    void *arg;
}EventListenerDetails_t;

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void EventSourceFree(EventSource_t source);
static void EventFree(Event_t event);
static void EventListenerDetailsFree(EventListenerDetails_t *details);
static void RegisterEventListener(List_t *listenerList, EventListener_t callback, void *arg);
static void UnRegisterEventListener(List_t *listenerList, EventListener_t callback, void *arg);
static void FireEventListeners(List_t *listenerList, Event_t event, void *payload);
static char* EventUnregisteredToString(Event_t event, void *payload);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static List_t *sourcesList;
static List_t *globalListenersList;
static pthread_mutex_t eventsMutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
static char EVENTS[]="Events";

static EventSource_t eventsSource;
static Event_t       eventUnregistered;
/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
int EventsInit(void)
{
    /* Register types used by this module */
    ObjectRegisterType(EventListenerDetails_t);
    ObjectRegisterClass("Event_t", sizeof(struct Event_s), NULL);
    ObjectRegisterClass("EventSource_t", sizeof(struct EventSource_s), NULL);

    /* Create the sources and global event listeners list */
    sourcesList = ListCreate();
    globalListenersList = ListCreate();

    eventsSource = EventsRegisterSource(EVENTS);
    eventUnregistered = EventsRegisterEvent(eventsSource, "Unregistered", EventUnregisteredToString);
    return 0;
}

int EventsDeInit(void)
{
    eventsSource = NULL;
    eventUnregistered = NULL;
    ListFree(sourcesList, (void(*)(void*)) EventSourceFree);
    ListFree(globalListenersList, (void(*)(void*)) EventListenerDetailsFree);
    return 0;
}

void EventsRegisterListener(EventListener_t listener, void *arg)
{
    pthread_mutex_lock(&eventsMutex);
    RegisterEventListener(globalListenersList, listener, arg);
    pthread_mutex_unlock(&eventsMutex);
}

void EventsUnregisterListener(EventListener_t listener, void *arg)
{
    pthread_mutex_lock(&eventsMutex);
    UnRegisterEventListener(globalListenersList, listener, arg);
    pthread_mutex_unlock(&eventsMutex);   
}

EventSource_t EventsRegisterSource(char *name)
{
    EventSource_t result = NULL;
    pthread_mutex_lock(&eventsMutex);
    result = ObjectCreateType(EventSource_t);
    if (result)
    {
        result->name = strdup(name);
        result->listeners = ListCreate();
        result->events = ListCreate();
        ListAdd(sourcesList, result);
        LogModule(LOG_DEBUG, EVENTS, "New event source registered (%s)\n", name);
    }
    pthread_mutex_unlock(&eventsMutex);    
    return result;
}

void EventsUnregisterSource(EventSource_t source)
{
    pthread_mutex_lock(&eventsMutex);
    ListRemove(sourcesList, source);
    EventSourceFree(source);
    LogModule(LOG_DEBUG, EVENTS, "Event source unregistered (%s)\n", source->name);
    pthread_mutex_unlock(&eventsMutex);
}

EventSource_t EventsFindSource(char *name)
{
    ListIterator_t iterator;
    EventSource_t result = NULL;
    
    pthread_mutex_lock(&eventsMutex);
    for (ListIterator_Init(iterator, sourcesList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        EventSource_t source = (EventSource_t)ListIterator_Current(iterator);
        if (strcmp(source->name, name) == 0)
        {
            result = source;
            break;
        }
    }
    pthread_mutex_unlock(&eventsMutex);
    return result;
}

void EventsRegisterSourceListener(EventSource_t source, EventListener_t listener, void *arg)
{
    pthread_mutex_lock(&eventsMutex);
    RegisterEventListener(source->listeners, listener, arg);
    pthread_mutex_unlock(&eventsMutex);
}

void EventsUnregisterSourceListener(EventSource_t source, EventListener_t listener, void *arg)
{
   pthread_mutex_lock(&eventsMutex);
   UnRegisterEventListener(source->listeners, listener, arg);
   pthread_mutex_unlock(&eventsMutex);
}

Event_t EventsRegisterEvent(EventSource_t source, char *name, EventToString_t toString)
{
    Event_t result = NULL;
    pthread_mutex_lock(&eventsMutex);
    result = ObjectCreateType(Event_t);
    if (result)
    {
        result->name = strdup(name);
        result->source = source;
        result->toString = toString;
        result->listeners = ListCreate();
        ListAdd(source->events, result);
        LogModule(LOG_DEBUG, EVENTS, "New event registered (%s.%s)\n", source->name, name);
    }
    pthread_mutex_unlock(&eventsMutex);
    return result;
}

void EventsUnregisterEvent(Event_t event)
{
   pthread_mutex_lock(&eventsMutex);
   ListRemove(event->source->events, event);
   EventFree(event);
   LogModule(LOG_DEBUG, EVENTS, "Event unregistered (%s.%s)\n", event->source->name, event->name);   
   pthread_mutex_unlock(&eventsMutex);
}

Event_t EventsFindEvent(char *name)
{
    char *sourceName;
    int sourceNameLen;
    EventSource_t source = NULL;
    Event_t result = NULL;
    
    pthread_mutex_lock(&eventsMutex);
    for (sourceNameLen = 0; name[sourceNameLen];sourceNameLen ++)
    {
        if (name[sourceNameLen] == '.')
        {
            break;
        }
    }
    sourceName = ObjectAlloc(sourceNameLen + 1);
    if (sourceName)
    {
        memcpy(sourceName, name, sourceNameLen);
        ObjectFree(sourceName);
        source = EventsFindSource(sourceName);
        if (source)
        {
            ListIterator_t iterator;
            for (ListIterator_Init(iterator, source->events);
                 ListIterator_MoreEntries(iterator);
                 ListIterator_Next(iterator))
            {
                Event_t event = (Event_t)ListIterator_Current(iterator);
                if (strcmp(event->name, &name[sourceNameLen + 1]) == 0)
                {
                    result = event;
                    break;
                }
            }
        }
            
    }
    pthread_mutex_unlock(&eventsMutex);
    return result;
}

void EventsFireEventListeners(Event_t event, void *payload)
{
    pthread_mutex_lock(&eventsMutex);
    LogModule(LOG_DEBUG, EVENTS, "Firing event %s.%s\n", event->source->name, event->name);   
    FireEventListeners(globalListenersList, event, payload);
    FireEventListeners(event->source->listeners, event, payload);
    FireEventListeners(event->listeners, event, payload);   

    pthread_mutex_unlock(&eventsMutex);
}

void EventsRegisterEventListener(Event_t event, EventListener_t listener, void *arg)
{
   pthread_mutex_lock(&eventsMutex);
   RegisterEventListener(event->listeners, listener, arg);
   pthread_mutex_unlock(&eventsMutex);
}

void EventsUnregisterEventListener(Event_t event, EventListener_t listener, void *arg)
{
   pthread_mutex_lock(&eventsMutex);
   UnRegisterEventListener(event->listeners, listener, arg);
   pthread_mutex_unlock(&eventsMutex);
}

char *EventsEventToString(Event_t event, void *payload)
{
    char *result = NULL;
    if (payload && event->toString)
    {
        char *payloadStr = event->toString(event, payload);
        asprintf(&result, "%s.%s %s", event->source->name, event->name, payloadStr);
        free(payloadStr);
    }
    else
    {
        asprintf(&result, "%s.%s", event->source->name, event->name);
    }
    return result;
}

/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/
static void EventSourceFree(EventSource_t source)
{
   free(source->name);
   ListFree(source->listeners,(void (*)(void*))EventListenerDetailsFree);   
   ListFree(source->events, (void (*)(void*))EventFree);
   ObjectRefDec(source);
}

static void EventFree(Event_t event)
{
    if ((eventUnregistered) && (event != eventUnregistered))
    {
        EventsFireEventListeners(eventUnregistered, event);
    }
    free(event->name);
    ListFree(event->listeners,(void (*)(void*))EventListenerDetailsFree);
    ObjectRefDec(event);
}

static void EventListenerDetailsFree(EventListenerDetails_t *details)
{
    ObjectRefDec(details);
}

static void RegisterEventListener(List_t *listenerList, EventListener_t callback, void *arg)
{
    EventListenerDetails_t *details = ObjectCreateType(EventListenerDetails_t);
    if (details)
    {
        details->callback = callback;
        details->arg = arg;
        ListAdd(listenerList, details);
    }
}

static void UnRegisterEventListener(List_t *listenerList, EventListener_t callback, void *arg)
{
    ListIterator_t iterator;

    for (ListIterator_Init(iterator, listenerList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        EventListenerDetails_t *details = (EventListenerDetails_t*)ListIterator_Current(iterator);
        if ((details->callback == callback) && (details->arg == arg))
        {
            ListRemoveCurrent(&iterator);
            break;
        }
    }
}

static void FireEventListeners(List_t *listenerList, Event_t event, void *payload)
{
    ListIterator_t iterator;

    for (ListIterator_Init(iterator, listenerList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        EventListenerDetails_t *details = (EventListenerDetails_t*)ListIterator_Current(iterator);
        LogModule(LOG_DEBUG, EVENTS, "Sending event to %p\n", details);
        details->callback(details->arg, event, payload);
    }
}

static char* EventUnregisteredToString(Event_t event, void *payload)
{
    return EventsEventToString((Event_t)payload, NULL);
}
