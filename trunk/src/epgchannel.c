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

epgchannel.c

Used to send EPG information to interested parties.

*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <limits.h>
#include <time.h>

#include "main.h"
#include "types.h"

#include "list.h"
#include "logging.h"
#include "objects.h"
#include "messageq.h"
#include "epgchannel.h"

/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/                
#define CHECK_LISTENERS() \
    do{ \
        int count;\
        pthread_mutex_lock(&EPGChannelMutex); \
        count = ListCount(EPGChannelListeners); \
        pthread_mutex_unlock(&EPGChannelMutex);\
        if (count == 0) \
        {\
            LogModule(LOG_DIARRHEA, EPGCHANNEL, "Not creating message, no listeners!");\
            return 0;\
        }\
    }while(0)
    
/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void EPGChannelMessageDestructor(void *ptr);
static void EPGChannelSendMessage(EPGChannelMessage_t *msg);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static pthread_mutex_t EPGChannelMutex = PTHREAD_MUTEX_INITIALIZER;
static const char EPGCHANNEL[] = "EPGDChannel";
static List_t *EPGChannelListeners;
/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
int EPGChannelInit(void)
{
    ObjectRegisterTypeDestructor(EPGChannelMessage_t, EPGChannelMessageDestructor);
    EPGChannelListeners = ListCreate();
    return 0;
}


int EPGChannelDeInit()
{
    ListFree(EPGChannelListeners, (void(*)(void*))MessageQDestroy);
    return 0;
}

int EPGChannelRegisterListener(MessageQ_t msgQ)
{
    pthread_mutex_lock(&EPGChannelMutex);
    ListAdd(EPGChannelListeners, msgQ);
    pthread_mutex_unlock(&EPGChannelMutex);
    return 0;
}

int EPGChannelUnregisterListener(MessageQ_t msgQ)
{
    pthread_mutex_lock(&EPGChannelMutex);
    ListRemove(EPGChannelListeners, msgQ);
    pthread_mutex_unlock(&EPGChannelMutex);
    return 0;
}


int EPGChannelNewEvent(EPGEventRef_t *eventRef, struct tm *startTime, struct tm *endTime, bool ca)
{
    EPGChannelMessage_t *msg;

    CHECK_LISTENERS();
    
    msg = ObjectCreateType(EPGChannelMessage_t);
    msg->type = EPGChannelMessageType_Event;
    msg->eventRef = *eventRef;
    msg->data.event.startTime = *startTime;
    msg->data.event.endTime = *endTime;
    msg->data.event.ca = ca;
    EPGChannelSendMessage(msg);
    return 0;
}


int EPGChannelNewRating(EPGEventRef_t *eventRef, char *system, char *rating)
{
    EPGChannelMessage_t *msg;

    CHECK_LISTENERS();
    
    msg = ObjectCreateType(EPGChannelMessage_t);
    msg->type = EPGChannelMessageType_Rating;
    msg->eventRef = *eventRef;
    msg->data.rating.system = strdup(system);
    msg->data.rating.rating = strdup(rating);
    EPGChannelSendMessage(msg);
    return 0;
}


int EPGChannelNewDetail(EPGEventRef_t *eventRef, char *lang, char * name, char *value)
{
    EPGChannelMessage_t *msg;    
    EPGEventDetail_t *eventDetail;
    CHECK_LISTENERS();

    msg = ObjectCreateType(EPGChannelMessage_t);
    msg->type = EPGChannelMessageType_Detail;
    msg->eventRef = *eventRef;
    memcpy(msg->data.detail.lang, lang, sizeof(eventDetail->lang));
    msg->data.detail.name = strdup(name);
    msg->data.detail.value = strdup(value);
    EPGChannelSendMessage(msg);
    return 0;
}


/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/
static void EPGChannelSendMessage(EPGChannelMessage_t *msg)
{
    ListIterator_t iterator;
    pthread_mutex_lock(&EPGChannelMutex);
    for (ListIterator_Init(iterator, EPGChannelListeners); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        MessageQ_t msgQ = (MessageQ_t)ListIterator_Current(iterator);
        MessageQSend(msgQ, msg);
    }
    pthread_mutex_unlock(&EPGChannelMutex);
    ObjectRefDec(msg);
}

static void EPGChannelMessageDestructor(void *ptr)
{
    EPGChannelMessage_t *msg = ptr;
    switch(msg->type)
    {
        case EPGChannelMessageType_Event:
            break;
        case EPGChannelMessageType_Detail:
            free(msg->data.detail.name);
            free(msg->data.detail.value);            
            break;
        case EPGChannelMessageType_Rating:
            free(msg->data.rating.system);
            free(msg->data.rating.rating);            
            break;
    }
}
