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
#include "yaml.h"
/**
 * @defgroup Events Event Management
 * The Events module is used as means for any modue to register events of
 * interest and allow other modules to listen for those events, all events from
 * a source or any event.
 * Events are located based on the following naming convention:
 * \<EventSource\>.\<EventName\>
 * The '.' is used as the delimiter and should not appear in event source names.
 * The source and event names should follow the Pascal or UpperCamelCase naming
 * convention.
 *
 * The Events module itself exports the single event "Events.Unregistered", with
 * the event being destroyed as the payload, to inform interested parties when an 
 * event is destroyed. 
 *
 * \section events Events Exported
 *
 * \li \ref unregistered Sent when an event is being unregistered.
 *
 * \subsection unregistered Events.Unregistered
 * This event is fired just before the event is removed from the source. \n
 * \par
 * \c payload = The event being unregistered.
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
 * @param document YAML Document to add nodes to.
 * @param event The event to convert.
 * @param payload The payload for the event to convert.
 * @return A node id to be mapped to from the event "Details" key.
 */
typedef int (*EventToString_t)(yaml_document_t *document, Event_t event, void *payload);

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
 * Register a listener callback using the name.
 * The name can be "" for a global listener or
 * "<source name>" to register a source listener or
 * "<source name>.<event name>" to register a listener for a specific event.
 * @param name The name of the event to register with.
 * @param listener The callback function to register.
 * @param arg The user defined argument to pass to the callback when an event is fired.
 */ 
void EventsRegisterListenerByName(const char *event, EventListener_t listener, void *arg);

/**
 * Unregister a listener callback using the name.
 * @param name The name of the event to unregister with.
 * @param listener The callback function to unregister.
 * @param arg The user defined argument to pass to the callback when an event is fired.
 */
void EventsUnregisterListenerByName(const char *event, EventListener_t listener, void *arg);

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

/**
 * Find an event source with the given name.
 * @param name The name of the event source to find.
 * @return An EventSource_t or NULL if no source matched the supplied name.
 */
EventSource_t EventsFindSource(const char *name);

/**
 * Register a listener for a specific source.
 * @param source The source to register with.
 * @param listener The callback function to register.
 * @param arg The user defined argument to pass to the callback when an event is fired.
 */
void EventsRegisterSourceListener(EventSource_t source, EventListener_t listener, void *arg);
/**
 * Unregister a listener from receiving events from a source.
 * @param source The source to unregister with.
 * @param listener The callback function to unregister.
 * @param arg The user defined argument to pass to the callback when an event is fired.
 */
void EventsUnregisterSourceListener(EventSource_t source, EventListener_t listener, void *arg);

/**
 * Register a new event with an event source.
 * The toString function is used for debugging purposes and to allow the event
 * to be translated into useful information for external applications that may
 * receive event information over TCP for example.
 *
 * @param source The source the event is linked to.
 * @param name The name of the event.
 * @param toString A function to return a textual representation of the event.
 */
Event_t EventsRegisterEvent(EventSource_t source, char *name, EventToString_t toString);

/**
 * Unregisters an event.
 * @param event The event to unregister from its assocated source.
 */
void EventsUnregisterEvent(Event_t event);

/**
 * Given a name in the form \<Source\>.\<Event\> find the Event_t object and return it.
 * @param name The fully qualified name of the event to find.
 * @return An Event_t object or NULL if the event could not be found.
 */
Event_t EventsFindEvent(const char *name);

/**
 * Calls all listeners that have register to receive events in the following order
 * - Global listeners
 * - Source listeners
 * - Event Listeners
 *
 * @note All callbacks are called on the calling thread!
 *
 * @param event The event to fire.
 * @param payload The private information associated with the event.
 */
void EventsFireEventListeners(Event_t event, void *payload);

/**
 * Register a listener for a specific event.
 * @param event The event to register with.
 * @param listener The callback function to register.
 * @param arg The user defined argument to pass to the callback when an event is fired.
 */
void EventsRegisterEventListener(Event_t event, EventListener_t listener, void *arg);

/**
 * Unregister a listener from receiving an event.
 * @param event The event to unregister with.
 * @param listener The callback function to unregister.
 * @param arg The user defined argument to pass to the callback when an event is fired.
 */
void EventsUnregisterEventListener(Event_t event, EventListener_t listener, void *arg);

/**
 * This function converts the event into a human readable form by combining the 
 * name of the event, with the output of the event specifc toString function 
 * (if supplied when the event was created).
 * @param event The event to convert.
 * @param payload The payload of the event.
 * @return A string containing "\<SourceName\>.\<EventName\>" if no toString 
 * function was supplied when the event was created, or 
 * "\<SourceName\>.\<EventName\> \<toString output\>" if a toString function was
 * supplied.
 */
char *EventsEventToString(Event_t event, void *payload);

/** @} */
#endif



