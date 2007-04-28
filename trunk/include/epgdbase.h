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

epgdbase.h

EPG Database Functions and structures.

*/

#ifndef _EPGDBASE_H
#define _EPGDBASE_H
#include <time.h>
#include "types.h"

#define EPG_EVENT_DETAIL_TITLE       "title"
#define EPG_EVENT_DETAIL_DESCRIPTION "description"

typedef struct EPGServiceRef_s
{
    unsigned int netId;
    unsigned int tsId;
    unsigned int serviceId;
}EPGServiceRef_t;

typedef struct EPGEvent_s
{
    EPGServiceRef_t serviceRef;
    unsigned int eventId;
    struct tm    startTime;
    struct tm    endTime;
    bool         ca;
}EPGEvent_t;

typedef struct EPGEventRating_s
{
    int id;
    char *system;
    char *rating;
}EPGEventRating_t;

typedef struct EPGEventDetail_s
{
    int id;
    char lang[4]; /**< 3 character language code + \0 */
    char *name;   /**< Name of the information in question, ie title/description/director etc. */
    char *value;  /**< The actual information. */
}EPGEventDetail_t;

typedef void *EPGDBaseEnumerator_t;

int EPGDBaseInit(int adapter);
int EPGDBaseDeInit(void);

int EPGDBaseTransactionStart(void);
int EPGDBaseTransactionCommit(void);

int EPGDBaseEventAdd(EPGEvent_t *event);
int EPGDBaseEventRemove(EPGServiceRef_t *serviceRef, unsigned int eventId);
int EPGDBaseEventCountAll();
int EPGDBaseEventCountService(EPGServiceRef_t *serviceRef);
EPGDBaseEnumerator_t EPGDBaseEventEnumeratorGetAll();
EPGDBaseEnumerator_t EPGDBaseEventEnumeratorGetService(EPGServiceRef_t *serviceRef);
EPGEvent_t *EPGDBaseEventGetNext(EPGDBaseEnumerator_t enumerator);


int EPGDBaseRatingAdd(EPGServiceRef_t *serviceRef, unsigned int eventId, char *system, char *rating);
int EPGDBaseRatingRemoveAll(EPGServiceRef_t *serviceRef, unsigned int eventId);
int EPGDBaseRatingCount(EPGServiceRef_t *serviceRef, unsigned int eventId);
EPGDBaseEnumerator_t EPGDBaseRatingEnumeratorGet(EPGServiceRef_t *serviceRef, unsigned int eventId);
EPGEventRating_t *EPGDBaseRatingGetNext(EPGDBaseEnumerator_t enumerator);

int EPGDBaseDetailAdd(EPGServiceRef_t *serviceRef, unsigned int eventId, char *lang, char * name, char *value);
int EPGDBaseDetailRemoveAll(EPGServiceRef_t *serviceRef, unsigned int eventId);
int EPGDBaseDetailCount(EPGServiceRef_t *serviceRef, unsigned int eventId);
EPGDBaseEnumerator_t EPGDBaseDetailGet(EPGServiceRef_t *serviceRef, unsigned int eventId, char *name);
EPGDBaseEnumerator_t EPGDBaseDetailEnumeratorGet(EPGServiceRef_t *serviceRef, unsigned int eventId);
EPGEventDetail_t *EPGDBaseDetailGetNext(EPGDBaseEnumerator_t enumerator);
void EPGDBaseEnumeratorDestroy(EPGDBaseEnumerator_t enumerator);
#endif
