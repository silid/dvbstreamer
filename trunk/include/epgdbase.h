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
#include "epgtypes.h"

/**
 * @defgroup EPGDB EPG Database
 * This module is used to hold all EPG information for an adapter.\n
 * Event information older than 24 hours is removed by a thread that runs every 
 * hour.
 * @{
 */


/**
 * Handle to enumerate events.
 */
typedef void *EPGDBaseEnumerator_t;

/**
 * @internal
 * Initialises the EPG database for a specific adapter.
 * @param adapter The adapter number to initialise the database for.
 * @returns 0 on success.
 */
int EPGDBaseInit(int adapter);
/**
 * @internal
 * Deinitialises the EPG database.
 */
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

/**@}*/
#endif
