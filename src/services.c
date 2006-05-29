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
 
services.c
 
Manage services and PIDs.
 
*/
#include <stdlib.h>
#include <string.h>
#include "dbase.h"
#include "multiplexes.h"
#include "services.h"
#include "logging.h"

int ServiceCount()
{
    int result = -1;
    STATEMENT_INIT;

    STATEMENT_PREPARE("SELECT count() FROM " SERVICES_TABLE ";");
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

int ServiceDelete(Service_t  *service)
{
    STATEMENT_INIT;

    STATEMENT_PREPAREVA("DELETE FROM " SERVICES_TABLE " "
                        "WHERE " SERVICE_MPLEXFREQ "=%d AND " SERVICE_ID "=%d;",
                        service->multiplexfreq, service->id);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();

    STATEMENT_FINALIZE();
    return 0;
}

int ServiceAdd(int multiplexfreq, char *name, int id, int pmtversion, int pmtpid)
{
    STATEMENT_INIT;

    STATEMENT_PREPAREVA("INSERT INTO "SERVICES_TABLE "("
                        SERVICE_MPLEXFREQ ","
                        SERVICE_ID ","
                        SERVICE_PMTVERSION ","
                        SERVICE_PMTPID ","
                        SERVICE_NAME ")"
                        "VALUES (%d,%d,%d,%d,'%q');",
                        multiplexfreq, id, pmtversion, pmtpid, name);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();
    RETURN_RC_ON_ERROR;

    STATEMENT_FINALIZE();
    return 0;
}

int ServicePMTVersionSet(Service_t  *service, int pmtversion)
{
    STATEMENT_INIT;

    STATEMENT_PREPAREVA("UPDATE " SERVICES_TABLE " "
                        "SET " SERVICE_PMTVERSION "=%d "
                        "WHERE " SERVICE_MPLEXFREQ "=%d AND " SERVICE_ID "=%d;",
                        pmtversion, service->multiplexfreq,  service->id);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();
    if (rc == SQLITE_DONE)
    {
        service->pmtversion = pmtversion;
        rc = SQLITE_OK;
    }
    else
    {
        PRINTLOG_SQLITE3ERROR();
    }
    STATEMENT_FINALIZE();
    return rc;
}


int ServicePMTPIDSet(Service_t  *service, int pmtpid)
{
    STATEMENT_INIT;

    STATEMENT_PREPAREVA("UPDATE " SERVICES_TABLE " "
                        "SET " SERVICE_PMTPID "=%d "
                        "WHERE " SERVICE_MPLEXFREQ "=%d AND " SERVICE_ID "=%d;",
                        pmtpid, service->multiplexfreq,  service->id);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();
    if (rc == SQLITE_DONE)
    {
        printlog(LOG_DEBUGV,"Updated 0x%04x %d\n", service->id, service->multiplexfreq);
        service->pmtpid = pmtpid;
        rc = SQLITE_OK;
    }
    else
    {
        PRINTLOG_SQLITE3ERROR();
    }
    STATEMENT_FINALIZE();
    return rc;
}

int ServiceNameSet(Service_t  *service, char *name)
{
    STATEMENT_INIT;

    STATEMENT_PREPAREVA("UPDATE " SERVICES_TABLE " "
                        "SET " SERVICE_NAME "='%q' "
                        "WHERE " SERVICE_MPLEXFREQ "=%d AND " SERVICE_ID "=%d;",
                        name, service->multiplexfreq,  service->id);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();
    if (rc == SQLITE_DONE)
    {
        printlog(LOG_DEBUGV,"Updated 0x%04x %d\n", service->id, service->multiplexfreq);
		free(service->name);
		service->name = strdup(name);
        rc = SQLITE_OK;
    }
    else
    {
        PRINTLOG_SQLITE3ERROR();
    }
    STATEMENT_FINALIZE();
    return rc;
}

Service_t *ServiceFindName(char *name)
{
    STATEMENT_INIT;
    Service_t *result;

    STATEMENT_PREPAREVA("SELECT "	SERVICE_MPLEXFREQ ","
                        SERVICE_ID ","
                        SERVICE_NAME ","
                        SERVICE_PMTVERSION ","
                        SERVICE_PMTPID " "
                        "FROM " SERVICES_TABLE " WHERE " SERVICE_NAME "='%q';",
                        name);
    RETURN_ON_ERROR(NULL);

    result = ServiceGetNext((ServiceEnumerator_t) stmt);
    STATEMENT_FINALIZE();

    return result;
}

Service_t *ServiceFindId(Multiplex_t *multiplex, int id)
{
    STATEMENT_INIT;
    Service_t *result;

    STATEMENT_PREPAREVA("SELECT "	SERVICE_MPLEXFREQ ","
                        SERVICE_ID ","
                        SERVICE_NAME ","
                        SERVICE_PMTVERSION ","
                        SERVICE_PMTPID " "
                        "FROM " SERVICES_TABLE " WHERE " SERVICE_MPLEXFREQ "=%d AND " SERVICE_ID "=%d;",
                        multiplex->freq, id);
    RETURN_ON_ERROR(NULL);

    result = ServiceGetNext((ServiceEnumerator_t) stmt);
    STATEMENT_FINALIZE();
    return result;
}

ServiceEnumerator_t ServiceEnumeratorGet()
{
    STATEMENT_INIT;
    STATEMENT_PREPARE("SELECT "	SERVICE_MPLEXFREQ ","
                      SERVICE_ID ","
                      SERVICE_NAME ","
                      SERVICE_PMTVERSION ","
                      SERVICE_PMTPID " "
                      "FROM " SERVICES_TABLE ";");
    RETURN_ON_ERROR(NULL);
    return stmt;
}

int ServiceForMultiplexCount(int freq)
{
    STATEMENT_INIT;
    int result = -1;

    STATEMENT_PREPAREVA("SELECT count() "
                        "FROM " SERVICES_TABLE " WHERE " SERVICE_MPLEXFREQ "=%d;",
                        freq);
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

ServiceEnumerator_t ServiceEnumeratorForMultiplex(int freq)
{
    STATEMENT_INIT;

    STATEMENT_PREPAREVA("SELECT "	SERVICE_MPLEXFREQ ","
                        SERVICE_ID ","
                        SERVICE_NAME ","
                        SERVICE_PMTVERSION ","
                        SERVICE_PMTPID " "
                        "FROM " SERVICES_TABLE " WHERE " SERVICE_MPLEXFREQ"=%d;",
                        freq);
    RETURN_ON_ERROR(NULL);

    return stmt;
}

void ServiceEnumeratorDestroy(ServiceEnumerator_t enumerator)
{
    int rc;
    sqlite3_stmt *stmt = (sqlite3_stmt *)enumerator;
    STATEMENT_FINALIZE();
}

Service_t *ServiceGetNext(ServiceEnumerator_t enumerator)
{
    sqlite3_stmt *stmt = (sqlite3_stmt *)enumerator;
    int rc;

    STATEMENT_STEP();
    if (rc == SQLITE_ROW)
    {
        Service_t *service = NULL;
        char *name;

        service = calloc(1, sizeof(Service_t));
        service->multiplexfreq = STATEMENT_COLUMN_INT( 0);
        service->id = STATEMENT_COLUMN_INT( 1);
        name = STATEMENT_COLUMN_TEXT( 2);
        if (name)
        {
            service->name = calloc(strlen(name) + 1, 1);
            strcpy(service->name, name);
        }
        service->pmtversion = STATEMENT_COLUMN_INT( 3);
        service->pmtpid = STATEMENT_COLUMN_INT( 4);

        return service;
    }

    PRINTLOG_SQLITE3ERROR();
    return NULL;
}

void ServiceFree(Service_t *service)
{
    if (service->name)
    {
        free(service->name);
    }
    free(service);
}

int ServicePIDAdd(Service_t *service, int pid, int type, int subtype, int pmtversion)
{
    STATEMENT_INIT;

    STATEMENT_PREPAREVA("INSERT INTO " PIDS_TABLE " "
                        "VALUES (%d,%d,%d,%d,%d,%d);",
                        service->multiplexfreq, service->id,
                        pid, type, subtype, pmtversion);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();
    STATEMENT_FINALIZE();
    return rc;
}

int ServicePIDRemove(Service_t *service)
{
    STATEMENT_INIT;

    STATEMENT_PREPAREVA("DELETE FROM " PIDS_TABLE " "
                        "WHERE " PID_MPLEXFREQ "=%d AND " PID_SERVICEID "=%d;",
                        service->multiplexfreq, service->id);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();

    STATEMENT_FINALIZE();
    return 0;
}

int ServicePIDCount(Service_t *service)
{
    STATEMENT_INIT;
    int result = -1;

    STATEMENT_PREPAREVA("SELECT count () FROM " PIDS_TABLE " "
                        "WHERE " PID_MPLEXFREQ "=%d AND " PID_SERVICEID "=%d;",
                        service->multiplexfreq, service->id);
    RETURN_ON_ERROR(-1);

    STATEMENT_STEP();
    if (rc == SQLITE_ROW)
    {
        result = STATEMENT_COLUMN_INT( 0);
        printlog(LOG_DEBUGV,"PID Count = %d\n", result);
        rc = 0;
    }
    STATEMENT_FINALIZE();
    return result;
}

int ServicePIDGet(Service_t *service, PID_t *pids, int *count)
{
    int i;
    STATEMENT_INIT;

    STATEMENT_PREPAREVA("SELECT "
                        PID_PID ","
                        PID_TYPE ","
                        PID_SUBTYPE ","
                        PID_PMTVERSION " "
                        "FROM " PIDS_TABLE " WHERE " PID_MPLEXFREQ "=%d AND " PID_SERVICEID "=%d;",
                        service->multiplexfreq, service->id);
    RETURN_RC_ON_ERROR;

    for (i = 0; i < *count; i ++)
    {
        STATEMENT_STEP();
        if (rc == SQLITE_ROW)
        {
            pids[i].pid = STATEMENT_COLUMN_INT( 0);
            pids[i].type = STATEMENT_COLUMN_INT( 1);
            pids[i].subtype = STATEMENT_COLUMN_INT( 2);
            pids[i].pmtversion = STATEMENT_COLUMN_INT( 3);
        }
        else
        {
            *count = i;
            break;
        }
    }
    STATEMENT_FINALIZE();
    return rc;
}
