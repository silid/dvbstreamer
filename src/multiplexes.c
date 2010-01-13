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

multiplexes.c

Manage multiplexes and tuning parameters.

*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dbase.h"
#include "multiplexes.h"
#include "services.h"
#include "logging.h"

/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/
#define MULTIPLEX_FIELDS MULTIPLEXES_TABLE "." MULTIPLEX_UID "," \
                         MULTIPLEXES_TABLE "." MULTIPLEX_TSID "," \
                         MULTIPLEXES_TABLE "." MULTIPLEX_NETID "," \
                         MULTIPLEXES_TABLE "." MULTIPLEX_TYPE "," \
                         MULTIPLEXES_TABLE "." MULTIPLEX_TUNINGPARAMS
                        
/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void MultiplexDestructor(void *ptr);
static void MultiplexListDestructor(void *ptr);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static int uidSeed;
static const char MULTIPLEXES[] = "Multiplexes";
static EventSource_t multiplexSource;
static Event_t multiplexAddedEvent;
static Event_t multiplexRemovedEvent;
/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/

int MultiplexInit(void)
{
    uidSeed = (int)time(NULL);
    multiplexSource = EventsRegisterSource("Multiplexes");
    multiplexAddedEvent = EventsRegisterEvent(multiplexSource, "Added", MultiplexEventToString);
    multiplexRemovedEvent = EventsRegisterEvent(multiplexSource, "Removed", MultiplexEventToString);    
    ObjectRegisterCollection(TOSTRING(MultiplexList_t),sizeof(Multiplex_t*), MultiplexListDestructor);
    return  ObjectRegisterTypeDestructor(Multiplex_t, MultiplexDestructor);
}

int MultiplexDeInit(void)
{
    return 0;
}

Multiplex_t *MultiplexFind(char *mux)
{
    Multiplex_t *result = NULL;
    int netId, tsId;
    /* Although the description says we check UID first it is simple to check
       for netid.tsid because it has the '.' and will pass the following if
       where as a simple number won't.
    */
    if (sscanf(mux, "%x.%x", &netId, &tsId) == 2)
    {
        result = MultiplexFindId(netId, tsId);
    }
    else
    {
        int n = atoi(mux);
        result = MultiplexFindUID(n);
    }
    
    return result;
}

Multiplex_t *MultiplexFindUID(int uid)
{
    Multiplex_t *result = NULL;
    STATEMENT_INIT;

    STATEMENT_PREPAREVA("SELECT " MULTIPLEX_FIELDS " "
                        "FROM " MULTIPLEXES_TABLE " WHERE " MULTIPLEX_UID "=%d;",uid);
    RETURN_ON_ERROR(NULL);

    result = MultiplexGetNext((MultiplexEnumerator_t)stmt);

    STATEMENT_FINALIZE();
    return result;
}

Multiplex_t *MultiplexFindId(int netid, int tsid)
{
    Multiplex_t *result = NULL;
    STATEMENT_INIT;

    STATEMENT_PREPAREVA("SELECT " MULTIPLEX_FIELDS " "
                        "FROM " MULTIPLEXES_TABLE
                        " WHERE " MULTIPLEX_NETID "=%d AND " MULTIPLEX_TSID "=%d;",
                        netid, tsid);
    RETURN_ON_ERROR(NULL);

    result = MultiplexGetNext((MultiplexEnumerator_t)stmt);

    STATEMENT_FINALIZE();
    return result;
}

MultiplexEnumerator_t MultiplexEnumeratorGet()
{
    STATEMENT_INIT;
    STATEMENT_PREPARE("SELECT " MULTIPLEX_FIELDS " "
                      "FROM " MULTIPLEXES_TABLE ";");
    RETURN_ON_ERROR(NULL);
    return stmt;
}

void MultiplexEnumeratorDestroy(MultiplexEnumerator_t enumerator)
{
    int rc;
    sqlite3_stmt *stmt = (sqlite3_stmt *)enumerator;
    STATEMENT_FINALIZE();
}

Multiplex_t *MultiplexGetNext(MultiplexEnumerator_t enumerator)
{
    int rc;
    sqlite3_stmt *stmt = (sqlite3_stmt *)enumerator;
    char *tmp;
    
    STATEMENT_STEP();
    if (rc == SQLITE_ROW)
    {
        Multiplex_t *multiplex;
        multiplex = MultiplexNew();
        multiplex->uid = STATEMENT_COLUMN_INT(0);
        multiplex->tsId = STATEMENT_COLUMN_INT(1);
        multiplex->networkId = STATEMENT_COLUMN_INT(2);
        multiplex->deliverySystem = STATEMENT_COLUMN_INT(3);
        multiplex->patVersion = -1;
        tmp = STATEMENT_COLUMN_TEXT(4);
        if (tmp)
        {
            multiplex->tuningParams = strdup(tmp);
        }

        return multiplex;
    }
    if (rc != SQLITE_DONE)
    {
        PRINTLOG_SQLITE3ERROR();
    }
    return NULL;
}

MultiplexList_t *MultiplexGetAll(void)
{
    STATEMENT_INIT;
    int i;
    int count;
    MultiplexList_t *list;

    count = DBaseCount(MULTIPLEXES_TABLE, NULL);
    
    STATEMENT_PREPARE("SELECT " MULTIPLEX_FIELDS " "
                      "FROM " MULTIPLEXES_TABLE ";");
    RETURN_ON_ERROR(NULL);

    list = (MultiplexList_t*)ObjectCollectionCreate(TOSTRING(MultiplexList_t),count);
    for (i = 0; i < count; i ++)
    {
        list->multiplexes[i] = MultiplexGetNext((MultiplexEnumerator_t)stmt);
    }
    STATEMENT_FINALIZE();
    return list;
}

int MultiplexAdd(DVBDeliverySystem_e delSys, char *tuningParams, Multiplex_t **mux)
{
    Multiplex_t *multiplex;
    STATEMENT_INIT;

    STATEMENT_PREPAREVA("INSERT INTO " MULTIPLEXES_TABLE "("
                        MULTIPLEX_UID ","
                        MULTIPLEX_TYPE ","
                        MULTIPLEX_TUNINGPARAMS ")"
                        "VALUES (%d, %d, %Q);", uidSeed, delSys, tuningParams);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();

    STATEMENT_FINALIZE();

    multiplex = MultiplexNew();
    multiplex->uid = uidSeed;
    uidSeed ++;
    multiplex->tsId = 0;
    multiplex->networkId = 0;
    multiplex->deliverySystem = delSys;
    multiplex->patVersion = -1;
    multiplex->tuningParams =  strdup(tuningParams);

    EventsFireEventListeners(multiplexAddedEvent, multiplex);
    *mux = multiplex;
    return rc;
}

int MultiplexDelete(Multiplex_t *multiplex)
{
    STATEMENT_INIT;
    STATEMENT_PREPAREVA("DELETE FROM " MULTIPLEXES_TABLE
                        " WHERE " MULTIPLEX_UID "=%d;", multiplex->uid);

    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();

    STATEMENT_FINALIZE();

    ServiceDeleteAll(multiplex);
    
    EventsFireEventListeners(multiplexRemovedEvent, multiplex);
    
    return 0;
}

int MultiplexTSIdSet(Multiplex_t *multiplex, int tsid)
{
    STATEMENT_INIT;
    STATEMENT_PREPAREVA("UPDATE " MULTIPLEXES_TABLE " "
                        "SET " MULTIPLEX_TSID "=%d "
                        "WHERE " MULTIPLEX_UID "=%d;", tsid, multiplex->uid);
    multiplex->tsId = tsid;
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();

    STATEMENT_FINALIZE();
    return rc;
}

int MultiplexNetworkIdSet(Multiplex_t *multiplex, int netid)
{
    STATEMENT_INIT;
    STATEMENT_PREPAREVA("UPDATE " MULTIPLEXES_TABLE " "
                        "SET " MULTIPLEX_NETID "=%d "
                        "WHERE " MULTIPLEX_UID "=%d;", netid, multiplex->uid);
    multiplex->networkId = netid;
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();

    STATEMENT_FINALIZE();
    return rc;
}

char *MultiplexEventToString(Event_t event,void * payload)
{
    char *result=NULL;
    Multiplex_t *mux = payload;
    if (asprintf(&result, "Multiplex UID: %d\n"
                          "Multiplex ID: %04x.%04x", 
                          mux->uid, mux->networkId, mux->tsId) == -1)
    {
        LogModule(LOG_INFO, MULTIPLEXES, "Failed to allocate memory for multiplex changed event description string.\n");
    }
    return result;
}

/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/

static void MultiplexDestructor(void *ptr)
{
    Multiplex_t *mux = ptr;
    if (mux->tuningParams)
    {
        free(mux->tuningParams);
    }
}

static void MultiplexListDestructor(void *ptr)
{
    MultiplexList_t *muxList = ptr;
    int i;
    for (i = 0; i < muxList->nrofMultiplexes; i ++)
    {
        ObjectRefDec(muxList->multiplexes[i]);
    }
}

