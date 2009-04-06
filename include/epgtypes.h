/*
Copyright (C) 2009  Adam Charrett

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

epgtypes.h

EPG Type Functions and structures.

*/

#ifndef _EPGTYPES_H
#define _EPGTYPES_H
#include <time.h>
#include "types.h"

/**
 * @defgroup EPGType EPG Types
 * This module is used to define the types used to descibe EPG events.\n
 * @{
 */

/**
 * Constant for an events title.
 */
#define EPG_EVENT_DETAIL_TITLE       "title"

/**
 * Constant for an events descripton.
 */
#define EPG_EVENT_DETAIL_DESCRIPTION "description"

/**
 * Structure used to identify a service in the EPG database.
 */
typedef struct EPGServiceRef_s
{
    unsigned int netId;     /**< Original network id. */
    unsigned int tsId;      /**< Transport stream id. */
    unsigned int serviceId; /**< Service id. */
}EPGServiceRef_t;

/**
 * Structure used to identify an event.
 */
typedef struct EPGEventRef_s
{
    EPGServiceRef_t serviceRef; /**< Service the event is happening on. */
    unsigned int eventId;    /**< Event id. */    
}EPGEventRef_t;

/** 
 * Structure used to describe an EPG event.
 */
typedef struct EPGEvent_s
{
    struct tm    startTime;     /**< Start time of the event.*/
    struct tm    endTime;       /**< Finish time of the event. */
    bool         ca;            /**< Whether the event is encrypted. */
}EPGEvent_t;

/**
 * Structure describing a rating for an event.
 */
typedef struct EPGEventRating_s
{
    char *system; /**< System the rating pertains to. */
    char *rating; /**< Rating of the event. */
}EPGEventRating_t;

/**
 * Structure describing a detail of an event.
 */
typedef struct EPGEventDetail_s
{
    char lang[4]; /**< 3 character language code + \0 */
    char *name;   /**< Name of the information in question, ie title/description/director etc. */
    char *value;  /**< The actual information. */
}EPGEventDetail_t;

/**
 * Initialises the EPG object types.
 * @return 0 on success.
 */
int EPGTypesInit(void);

/**
 * Deinitialises the EPG object types.
 * @return 0 on success.
 */
int EPGTypesDeInit(void);
/**@}*/
#endif
