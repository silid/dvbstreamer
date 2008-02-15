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

events.h

Events management functions

*/

#ifndef _DVBSTREAMER_EVENTS_H
#define _DVBSTREAMER_EVENTS_H

#include "types.h"

/**
 * @defgroup Events Event Management
 * The Events module is used as means for any modue to register events of
 * interest and allow other modules to listen for those events, all events from
 * a source or any event.
 * Events are located based on the following naming convention:
 * <EventSource>.<EventName>
 * The '.' is used as the delimiter and should not appear in event source names.
 * The source and event names should follow the Pascal or UpperCamelCase naming
 * convention.
 *@{
 */

/**
 * Event Source handle.
 * An event source has a number of events (Event_t instances) associated with it.
 */
typedef struct EventSource_s *EventSource_t;

/**
 * Event handle.
 * An event is associated with an event source (EventSource_t).
 */
typedef struct Event_s *Event_t;

/**
 * Pointer to a function that converts an event and its payload into a human 
 * readable string.
 */
typedef char *(*EventToString_t)(Event_t event, void *payload);

/**
 * Callback function that is to be executed when an event is fired.
 * @param arg A user defined argument to pass to the function.
 * @param event The event being fired.
 * @param payload The details of the event.
 */
typedef void (*EventListener_t)(void *arg, Event_t event, void *payload);

/**
 * @internal
 * Initialises the Events subsystem.
 * @returns 0 on success.
 */
int EventsInit(void);

/**
 * @internal
 * Deinitialises the Events subsystem.
 * @returns 0 on success.
 */
int EventsDeInit(void);

/**
 * Register a listener to receive ALL events.
 * @param listener The callback function to register.
 * @param arg The user defined argument to pass to the callback when an event is fired.
 */
void EventsRegisterListener(EventListener_t listener, void *arg);

/**
 * Unregister a listener from receiving all events.
 * @param listener The callback function to unregister.
 * @param arg The user defined argument to pass to the callback when an event is fired.
 */
void EventsUnregisterListener(EventListener_t listener, void *arg);

/**
 * Register a new event source. The name of the source must not contain 
 * @param name The name of the source.
 * @return An EventSource_t instance or NULL if the registration failed.
 */
EventSource_t EventsRegisterSource(char *name);

/**
 * Removes a previously registered source and all the sources associated events 
 * and listeners.
 * @param source The source to unregister.
 */
void EventsUnregisterSource(EventSource_t source);

EventSource_t EventsFindSource(char *name);

void EventsRegisterSourceListener(EventSource_t source, EventListener_t listener, void *arg);

void EventsUnregisterSourceListener(EventSource_t source, EventListener_t listener, void *arg);

Event_t EventsRegisterEvent(EventSource_t source, char *name, EventToString_t toString);

void EventsUnregisterEvent(Event_t event);

Event_t EventsFindEvent(char *name);

void EventsFireEventListeners(Event_t event, void *payload);

void EventsRegisterEventListener(Event_t event, EventListener_t listener, void *arg);

void EventsUnregisterEventListener(Event_t event, EventListener_t listener, void *arg);

/**
 * Default implementation of an EventToString_t functions.
 * This simply returns the name of the event as a combination of the source name
 * and the event name.
 * @param event The event to convert.
 * @param payload The payload of the event.
 * @return A string containing <SourceName>.<EventName>
 */
char *EventsEventToString(Event_t event, void *payload);

/** @} */
#endif



