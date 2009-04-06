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

epgchannel.h

EPG Functions and structures for sending/listening for EPG information.

*/

#ifndef _EPGCHANNEL_H
#define _EPGCHANNEL_H
#include "messageq.h"
#include "epgtypes.h"

/**
 * @defgroup EPGChannel EPG Channel
 * This module is used to send EPG information from the decoders to interested parties.\n
 * @{
 */
typedef enum {
    EPGChannelMessageType_Event,
    EPGChannelMessageType_Detail,        
    EPGChannelMessageType_Rating    
}EPGChannelMessageType;

typedef struct EPGChannelMessage_s{
    EPGChannelMessageType type;
    EPGEventRef_t eventRef;
    union {
        EPGEvent_t *event;
        EPGEventDetail_t *detail;
        EPGEventRating_t *rating;
    }data;
}EPGChannelMessage_t;

/**
 * Initialises the EPG object types.
 * @return 0 on success.
 */
int EPGChannelInit(void);

/**
 * Deinitialises the EPG object types.
 * @return 0 on success.
 */
int EPGChannelDeInit(void);

int EPGChannelRegisterListener(MessageQ_t msgQ);
int EPGChannelUnregisterListener(MessageQ_t msgQ);
int EPGChannelNewEvent(EPGEventRef_t *eventRef, struct tm *startTime, struct tm *endTime, bool ca);
int EPGChannelNewRating(EPGEventRef_t *eventRef, char *system, char *rating);
int EPGChannelNewDetail(EPGEventRef_t *eventRef, char *lang, char * name, char *value);
/**@}*/
#endif

