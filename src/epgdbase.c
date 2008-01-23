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

schedule.c

Plugin to collect EPG schedule information.

*/
#define _XOPEN_SOURCE
#define _GNU_SOURCE
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

#define DBASE_CONNECTION_GET EPGDBaseConnectionGet
#include "dbase.h"
#include "epgdbase.h"

#include "list.h"
#include "logging.h"
#include "objects.h"

/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/
#define EPGDBASE_VERSION_NAME "EPGDBaseVersion"
#define EPGDBASE_VERSION 0.1

#define EPGEVENT_FIELDS EPGEVENT_NETID "," EPGEVENT_TSID "," EPGEVENT_SERVICEID "," \
                        EPGEVENT_EVENTID "," EPGEVENT_STARTTIME "," EPGEVENT_ENDTIME  "," \
                        EPGEVENT_CA

#define EPGRATING_FIELDS EPGRATING_EVENTUID "," EPGRATING_STANDARD "," \
                         EPGRATING_RATING

#define EPGRATING_FIELDS_NOREF EPGRATING_STANDARD "," \
                         EPGRATING_RATING
                         
#define EPGDETAIL_FIELDS EPGDETAIL_EVENTUID "," EPGDETAIL_LANGUAGE "," \
                          EPGDETAIL_NAME "," EPGDETAIL_VALUE

#define EPGDETAIL_FIELDS_NOREF EPGDETAIL_LANGUAGE "," \
                          EPGDETAIL_NAME "," EPGDETAIL_VALUE                          

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static sqlite3 *EPGDBaseConnectionGet(void);
static void EPGEventRatingDestructor(void *arg);
static void EPGEventDetailDestructor(void *arg);

static long long int CreateEventUID(EPGServiceRef_t *serviceRef, unsigned int eventId);
static void *ReaperProcess(void *arg);
static int PurgeOldEvents(void);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static pthread_mutex_t EPGMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  ReaperCondVar = PTHREAD_COND_INITIALIZER;
static pthread_t       ReaperThread;
static bool            ReaperExit = FALSE;
static const char TimeFormat[] = "%Y-%m-%d %T";
static const char EPGDBASE[] = "EPGDBase";
static char EPGDBaseFile[PATH_MAX];

static pthread_key_t EPGDaseConnectionKey;

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
int EPGDBaseInit(int adapter)
{
    int rc;
    sqlite3 *EPGDBaseConnection;
    
    ObjectRegisterType(EPGEvent_t);
    ObjectRegisterTypeDestructor(EPGEventRating_t, EPGEventRatingDestructor);    
    ObjectRegisterTypeDestructor(EPGEventDetail_t, EPGEventDetailDestructor);

    sprintf(EPGDBaseFile, "%s/epg%d.db", DataDirectory, adapter);
    rc = sqlite3_open(EPGDBaseFile, &EPGDBaseConnection);
    if (rc == SQLITE_OK)
    {
        rc = sqlite3_exec(EPGDBaseConnection, "CREATE TABLE IF NOT EXISTS " EPGEVENTS_TABLE" ( "
                          EPGEVENT_FIELDS ","
                          "PRIMARY KEY ( "EPGEVENT_NETID "," EPGEVENT_TSID "," EPGEVENT_SERVICEID "," EPGEVENT_EVENTID ")"
                          ");", NULL, NULL, NULL);
        if (rc)
        {
            LogModule(LOG_ERROR, EPGDBASE, "Failed to create EPG Events table: %s\n", sqlite3_errmsg(EPGDBaseConnection));
            return rc;
        }

        rc = sqlite3_exec(EPGDBaseConnection, "CREATE TABLE IF NOT EXISTS " EPGRATINGS_TABLE" ( "
                         EPGRATING_EVENTUID   "," 
                         EPGRATING_STANDARD  ","
                         EPGRATING_RATING   ","
                         "PRIMARY KEY ( " EPGRATING_EVENTUID "," EPGRATING_STANDARD ")"
                         ");", NULL, NULL, NULL);
        if (rc)
        {
            LogModule(LOG_ERROR, EPGDBASE, "Failed to create EPG Ratings table: %s\n", sqlite3_errmsg(EPGDBaseConnection));
            return rc;
        }

        rc = sqlite3_exec(EPGDBaseConnection, "CREATE TABLE IF NOT EXISTS " EPGDETAILS_TABLE" ( "
                         EPGDETAIL_EVENTUID   "," 
                         EPGDETAIL_LANGUAGE  ","
                         EPGDETAIL_NAME  ","
                         EPGDETAIL_VALUE ","
                         "PRIMARY KEY ( " EPGDETAIL_EVENTUID "," EPGDETAIL_LANGUAGE "," EPGDETAIL_NAME ")"
                         ");", NULL, NULL, NULL);
        if (rc)
        {
            LogModule(LOG_ERROR, EPGDBASE, "Failed to create EPG Ratings table: %s\n", sqlite3_errmsg(EPGDBaseConnection));
            return rc;
        }

        rc = sqlite3_exec(EPGDBaseConnection, "CREATE INDEX IF NOT EXISTS EPGDetails_eventuid_name_index ON "
                         EPGDETAILS_TABLE" ( " EPGDETAIL_EVENTUID "," EPGDETAIL_NAME ");", 
                         NULL, NULL, NULL);
        if (rc)
        {
            LogModule(LOG_ERROR, EPGDBASE, "Failed to create EPG details index: %s\n", sqlite3_errmsg(EPGDBaseConnection));
            return rc;
        }
        pthread_key_create(&EPGDaseConnectionKey, (void(*)(void *))sqlite3_close);
        pthread_create(&ReaperThread, NULL, ReaperProcess, NULL);

        sqlite3_close(EPGDBaseConnection);        
    }
    return rc;
}


int EPGDBaseDeInit()
{
    ReaperExit = TRUE;
    pthread_cond_signal(&ReaperCondVar);
    pthread_join(ReaperThread, NULL);
    return 0;
}

int EPGDBaseTransactionStart(void)
{
    pthread_mutex_lock(&EPGMutex);
    return  sqlite3_exec(EPGDBaseConnectionGet(), "BEGIN TRANSACTION;", NULL, NULL, NULL);
}

int EPGDBaseTransactionCommit(void)
{
    int rc = sqlite3_exec(EPGDBaseConnectionGet(), "COMMIT TRANSACTION;", NULL, NULL, NULL);
    pthread_mutex_unlock(&EPGMutex);
    return rc;
}

int EPGDBaseEventAdd(EPGEvent_t *event)
{
    STATEMENT_INIT;
    time_t startTime;
    time_t endTime;

    startTime = timegm(&event->startTime);
    endTime = timegm(&event->endTime);

    STATEMENT_PREPAREVA("INSERT OR REPLACE INTO " EPGEVENTS_TABLE"("EPGEVENT_FIELDS") "
                        "VALUES (%d,%d,%d,%d,%d,%d,%d);",
                        event->serviceRef.netId,event->serviceRef.tsId,event->serviceRef.serviceId,
                        event->eventId, startTime, endTime, event->ca);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();
    RETURN_RC_ON_ERROR;

    STATEMENT_FINALIZE();
    return 0;
}

int EPGDBaseEventRemove(EPGServiceRef_t *serviceRef, unsigned int eventId)
{
    STATEMENT_INIT;

    STATEMENT_PREPAREVA("DELETE FROM " EPGEVENTS_TABLE " "
                        "WHERE " EPGEVENT_NETID "=%u AND " 
                                 EPGEVENT_TSID "=%u AND " 
                                 EPGEVENT_SERVICEID "=%u AND "
                                 EPGEVENT_EVENTID "=%u;",
                        serviceRef->netId, 
                        serviceRef->tsId, 
                        serviceRef->serviceId, 
                        eventId);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();

    STATEMENT_FINALIZE();
    return 0;   
}

int EPGDBaseEventCountAll()
{
    int result = -1;
    STATEMENT_INIT;

    STATEMENT_PREPARE("SELECT count() FROM " EPGEVENTS_TABLE ";");
    RETURN_ON_ERROR(-1);

    STATEMENT_STEP();
    if (rc == SQLITE_ROW)
    {
        result = STATEMENT_COLUMN_INT( 0);
        rc = 0;
    }
    STATEMENT_FINALIZE();
    return result;
}

int EPGDBaseEventCountService(EPGServiceRef_t *serviceRef)
{
    int result = -1;
    STATEMENT_INIT;

    STATEMENT_PREPAREVA("SELECT count() FROM " EPGEVENTS_TABLE " WHERE "
        EPGEVENT_NETID "=%u AND " EPGEVENT_TSID "=%u AND " EPGEVENT_SERVICEID "=%u;",
        serviceRef->netId, serviceRef->tsId, serviceRef->serviceId);
    RETURN_ON_ERROR(-1);

    STATEMENT_STEP();
    if (rc == SQLITE_ROW)
    {
        result = STATEMENT_COLUMN_INT( 0);
        rc = 0;
    }
    STATEMENT_FINALIZE();
    return result;
}

EPGDBaseEnumerator_t EPGDBaseEventEnumeratorGetAll()
{
    STATEMENT_INIT;

    STATEMENT_PREPARE("SELECT " EPGEVENT_FIELDS " "
                        "FROM " EPGEVENTS_TABLE ";");
    RETURN_ON_ERROR(NULL);

    return stmt;
}

EPGDBaseEnumerator_t EPGDBaseEventEnumeratorGetService(EPGServiceRef_t *serviceRef)
{
    STATEMENT_INIT;

    STATEMENT_PREPAREVA("SELECT " EPGEVENT_FIELDS " "
                        "FROM " EPGEVENTS_TABLE " "
                        "WHERE " EPGEVENT_NETID "=%u AND " 
                                 EPGEVENT_TSID "=%u AND " 
                                 EPGEVENT_SERVICEID "=%u;",
                        serviceRef->netId, serviceRef->tsId, serviceRef->serviceId);
    RETURN_ON_ERROR(NULL);

    return stmt;
}

EPGEvent_t *EPGDBaseEventGetNext(EPGDBaseEnumerator_t enumerator)
{
    sqlite3_stmt *stmt = (sqlite3_stmt *)enumerator;
    int rc;

    STATEMENT_STEP();
    if (rc == SQLITE_ROW)
    {
        EPGEvent_t *event = NULL;
        time_t temptime;
        struct tm *temptm;
        
        event = ObjectCreateType(EPGEvent_t);
        event->serviceRef.netId = (unsigned int)STATEMENT_COLUMN_INT( 0);
        event->serviceRef.tsId = (unsigned int)STATEMENT_COLUMN_INT( 1);
        event->serviceRef.serviceId = (unsigned int)STATEMENT_COLUMN_INT( 2);
        event->eventId = (unsigned int)STATEMENT_COLUMN_INT( 3);
        temptime = STATEMENT_COLUMN_INT(4);
        temptm = gmtime(&temptime);
        event->startTime = *temptm;
        temptime = STATEMENT_COLUMN_INT(5);
        temptm = gmtime(&temptime);
        event->endTime = *temptm;
        event->ca = STATEMENT_COLUMN_INT( 6) ? TRUE: FALSE;
        return event;
    }

    if (rc != SQLITE_DONE)
    {
        PRINTLOG_SQLITE3ERROR();
    }
    return NULL;
}


int EPGDBaseRatingAdd(EPGServiceRef_t *serviceRef, unsigned int eventId, char *system, char *rating)
{
    long long int eventUID;
    STATEMENT_INIT;
    eventUID  = CreateEventUID(serviceRef, eventId);
    
    STATEMENT_PREPAREVA("INSERT OR REPLACE INTO " EPGRATINGS_TABLE "(" EPGRATING_FIELDS ") "
                        "VALUES (%lld,'%q','%q');",
                        eventUID, system, rating);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();
    RETURN_RC_ON_ERROR;

    STATEMENT_FINALIZE();
    return 0;
}

int EPGDBaseRatingRemoveAll(EPGServiceRef_t *serviceRef, unsigned int eventId)
{
    long long int eventUID;
    STATEMENT_INIT;
    eventUID  = CreateEventUID(serviceRef, eventId);

    STATEMENT_PREPAREVA("DELETE FROM " EPGRATINGS_TABLE " "
                        "WHERE " EPGRATING_EVENTUID "=%lld;",
                        eventUID);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();

    STATEMENT_FINALIZE();
    return 0;   
}

int EPGDBaseRatingCount(EPGServiceRef_t *serviceRef, unsigned int eventId)
{
    int result = -1;
    long long int eventUID;
    STATEMENT_INIT;
    eventUID  = CreateEventUID(serviceRef, eventId);

    STATEMENT_PREPAREVA("SELECT count() FROM " EPGRATINGS_TABLE " WHERE "
                        EPGRATING_EVENTUID "=%lld;", eventUID);
    RETURN_ON_ERROR(-1);

    STATEMENT_STEP();
    if (rc == SQLITE_ROW)
    {
        result = STATEMENT_COLUMN_INT( 0);
        rc = 0;
    }
    STATEMENT_FINALIZE();
    return result;
}

EPGDBaseEnumerator_t EPGDBaseRatingEnumeratorGet(EPGServiceRef_t *serviceRef, unsigned int eventId)
{
    long long int eventUID;
    STATEMENT_INIT;
    eventUID  = CreateEventUID(serviceRef, eventId);

    STATEMENT_PREPAREVA("SELECT " EPGRATING_FIELDS_NOREF " "
                        "FROM " EPGRATINGS_TABLE" "
                        "WHERE " EPGRATING_EVENTUID "=%lld;",
                        eventUID);
    RETURN_ON_ERROR(NULL);

    return stmt;
}

EPGEventRating_t *EPGDBaseRatingGetNext(EPGDBaseEnumerator_t enumerator)
{
    sqlite3_stmt *stmt = (sqlite3_stmt *)enumerator;
    int rc;

    STATEMENT_STEP();
    if (rc == SQLITE_ROW)
    {
        EPGEventRating_t *rating = NULL;
        char *temp;
        rating = ObjectCreateType(EPGEventRating_t);
        temp = STATEMENT_COLUMN_TEXT( 0);
        rating->system= strdup(temp);
        temp = STATEMENT_COLUMN_TEXT( 1);
        rating->rating = strdup(temp);        
        return rating;
    }

    if (rc != SQLITE_DONE)
    {
        PRINTLOG_SQLITE3ERROR();
    }
    return NULL;
}

int EPGDBaseDetailAdd(EPGServiceRef_t *serviceRef, unsigned int eventId, char *lang, char * name, char *value)
{
    long long int eventUID;
    STATEMENT_INIT;
    eventUID  = CreateEventUID(serviceRef, eventId);
    
    STATEMENT_PREPAREVA("INSERT OR REPLACE INTO " EPGDETAILS_TABLE "(" EPGDETAIL_FIELDS ") "
                        "VALUES (%lld,'%q','%q','%q');",
                        eventUID, lang, name, value);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();
    RETURN_RC_ON_ERROR;

    STATEMENT_FINALIZE();
    return 0;
}

int EPGDBaseDetailRemoveAll(EPGServiceRef_t *serviceRef, unsigned int eventId)
{
    long long int eventUID;
    STATEMENT_INIT;
    eventUID  = CreateEventUID(serviceRef, eventId);

    STATEMENT_PREPAREVA("DELETE FROM " EPGDETAILS_TABLE " "
                        "WHERE " EPGDETAIL_EVENTUID "=%lld;",
                        eventUID);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();

    STATEMENT_FINALIZE();
    return 0;   
}

int EPGDBaseDetailCount(EPGServiceRef_t *serviceRef, unsigned int eventId)
{
    int result = -1;
    long long int eventUID;
    STATEMENT_INIT;
    eventUID  = CreateEventUID(serviceRef, eventId);

    STATEMENT_PREPAREVA("SELECT count() FROM " EPGDETAILS_TABLE " WHERE "
                        EPGDETAIL_EVENTUID "=%lld;", eventUID);
    RETURN_ON_ERROR(-1);

    STATEMENT_STEP();
    if (rc == SQLITE_ROW)
    {
        result = STATEMENT_COLUMN_INT( 0);
        rc = 0;
    }
    STATEMENT_FINALIZE();
    return result;
}

EPGDBaseEnumerator_t EPGDBaseDetailGet(EPGServiceRef_t *serviceRef, unsigned int eventId, char *name)
{
    long long int eventUID;
    STATEMENT_INIT;
    eventUID  = CreateEventUID(serviceRef, eventId);

    STATEMENT_PREPAREVA("SELECT " EPGDETAIL_FIELDS_NOREF " "
                        "FROM " EPGDETAILS_TABLE " "
                        "WHERE " EPGDETAIL_EVENTUID "=%lld AND "
                                 EPGDETAIL_NAME "='%q';",
                        eventUID, name);
    RETURN_ON_ERROR(NULL);

    return stmt;
}

EPGDBaseEnumerator_t EPGDBaseDetailEnumeratorGet(EPGServiceRef_t *serviceRef, unsigned int eventId)
{
    long long int eventUID;
    STATEMENT_INIT;
    eventUID  = CreateEventUID(serviceRef, eventId);

    STATEMENT_PREPAREVA("SELECT " EPGDETAIL_FIELDS_NOREF " "
                        "FROM " EPGDETAILS_TABLE " "
                        "WHERE " EPGDETAIL_EVENTUID "=%lld;", eventUID);
    RETURN_ON_ERROR(NULL);

    return stmt;
}

EPGEventDetail_t *EPGDBaseDetailGetNext(EPGDBaseEnumerator_t enumerator)
{
    sqlite3_stmt *stmt = (sqlite3_stmt *)enumerator;
    int rc;

    STATEMENT_STEP();
    if (rc == SQLITE_ROW)
    {
        EPGEventDetail_t *detail = NULL;
        char *temp;
        detail = ObjectCreateType(EPGEventDetail_t);
        temp = STATEMENT_COLUMN_TEXT( 0);
        strcpy(detail->lang, temp);
        temp = STATEMENT_COLUMN_TEXT( 1);
        detail->name = strdup(temp);
        temp = STATEMENT_COLUMN_TEXT( 2);
        detail->value = strdup(temp);        
        return detail;
    }

    if (rc != SQLITE_DONE)
    {
        PRINTLOG_SQLITE3ERROR();
    }
    return NULL;
}

void EPGDBaseEnumeratorDestroy(EPGDBaseEnumerator_t enumerator)
{
    int rc;
    sqlite3_stmt *stmt = (sqlite3_stmt *)enumerator;
    STATEMENT_FINALIZE();
}

/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/
static sqlite3 *EPGDBaseConnectionGet(void)
{
    sqlite3 *connection = pthread_getspecific(EPGDaseConnectionKey);
    if (connection == NULL)
    {
        int rc = sqlite3_open(EPGDBaseFile, &connection);
        if (rc == SQLITE_OK)
        {
            pthread_setspecific(EPGDaseConnectionKey, connection);
        }
    }
    return connection;
}

static void EPGEventRatingDestructor(void *arg)
{
    EPGEventRating_t *rating = arg;
    free(rating->rating);
    free(rating->system);
}

static void EPGEventDetailDestructor(void *arg)
{
    EPGEventDetail_t *detail = arg;
    free(detail->name);
    free(detail->value);
}

static long long int CreateEventUID(EPGServiceRef_t *serviceRef, unsigned int eventId)
{
    long long int result;
    result = ((long long int)(serviceRef->netId     & 0xffff) << 48) |
             ((long long int)(serviceRef->tsId      & 0xffff) << 32) |
             ((long long int)(serviceRef->serviceId & 0xffff) << 16) |
              (long long int)(eventId               & 0xffff);

    return result;
}

static void *ReaperProcess(void *arg)
{
    struct timespec timeout;
    
    pthread_mutex_lock(&EPGMutex);
    while(!ReaperExit)
    {
        LogModule(LOG_INFO, EPGDBASE, "Purging old events!\n");
        PurgeOldEvents();
        LogModule(LOG_INFO, EPGDBASE, "Events purged.\n");
        if (!ReaperExit)
        {
            clock_gettime( CLOCK_REALTIME, &timeout);
            timeout.tv_sec += (24 * 60) * 60;
            pthread_cond_timedwait(&ReaperCondVar, &EPGMutex, &timeout);
        }
    }
    pthread_mutex_unlock(&EPGMutex);    
    LogModule(LOG_DEBUG, EPGDBASE, "Reaper thread exiting.\n");
    return NULL;
}

static int PurgeOldEvents(void)
{
    sqlite3 *connection = EPGDBaseConnectionGet();
    STATEMENT_INIT;
    time_t past24 = time(NULL);
    past24 -= (24*60) * 60; /* Delete everything that finished 24 hours ago */
    sqlite3_exec(connection, "BEGIN TRANSACTION;", NULL, NULL, NULL);

    LogModule(LOG_DEBUG, EPGDBASE, "Deleting from EPGEvents table.\n");
    /* Remove events that finished over 24 hours ago */
    STATEMENT_PREPAREVA("DELETE FROM " EPGEVENTS_TABLE " "
                        "WHERE " EPGEVENT_ENDTIME "<=%d;", 
                        past24);
    RETURN_RC_ON_ERROR;
    STATEMENT_STEP();
    LogModule(LOG_DEBUG, EPGDBASE, "Deleting from EPGDetails table.\n");
    /* Remove orphaned details and ratings */
    sqlite3_exec(connection, "DELETE FROM " EPGDETAILS_TABLE " WHERE NOT EXISTS"
                                        "(SELECT * FROM " EPGEVENTS_TABLE " WHERE "
                                        "netid=((EPGDetails.eventuid>>48) &65535) AND "
                                        "tsid=((EPGDetails.eventuid >>32) & 65535) AND "
                                        "serviceid=((EPGDetails.eventuid>>16) & 65535) AND "
                                        "eventid=(EPGDetails.eventuid & 65535));", 
                                        NULL, NULL, NULL);
    LogModule(LOG_DEBUG, EPGDBASE, "Deleting from EPGRatings table.\n");
    sqlite3_exec(connection, "DELETE FROM " EPGRATINGS_TABLE " WHERE NOT EXISTS"
                                        "(SELECT * FROM " EPGEVENTS_TABLE " WHERE "
                                        "netid=((EPGDetails.eventuid>>48) &65535) AND "
                                        "tsid=((EPGDetails.eventuid >>32) & 65535) AND "
                                        "serviceid=((EPGDetails.eventuid>>16) & 65535) AND "
                                        "eventid=(EPGDetails.eventuid & 65535));", 
                                        NULL, NULL, NULL);    
    LogModule(LOG_DEBUG, EPGDBASE, "Committing.\n");
    sqlite3_exec(connection, "COMMIT TRANSACTION;", NULL, NULL, NULL);
    STATEMENT_FINALIZE();
    return 0;
}

