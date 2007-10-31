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
#include <stdio.h>

#include "dbase.h"
#include "multiplexes.h"
#include "services.h"
#include "logging.h"

/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/

#define SERVICE_FIELDS SERVICE_MULTIPLEXUID "," \
                       SERVICE_ID "," \
                       SERVICE_SOURCE "," \
                       SERVICE_CA "," \
                       SERVICE_TYPE "," \
                       SERVICE_NAME "," \
                       SERVICE_PMTVERSION "," \
                       SERVICE_PMTPID ","\
                       SERVICE_PCRPID "," \
                       SERVICE_PROVIDER "," \
                       SERVICE_DEFAUTHORITY " "

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/

static void ServiceDestructor(void * arg);

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/

int ServiceInit(void)
{
    return  ObjectRegisterTypeDestructor(Service_t, ServiceDestructor);
}

int ServiceDeinit(void)
{
    return 0;
}

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
                        "WHERE " SERVICE_MULTIPLEXUID "=%d AND " SERVICE_ID "=%d;",
                        service->multiplexUID, service->id);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();

    STATEMENT_FINALIZE();
    return 0;
}

int ServiceAdd(int uid, char *name, int id, int source, bool ca, ServiceType type, 
                    int pmtversion, int pmtpid, int pcrpid)
{
    STATEMENT_INIT;

    STATEMENT_PREPAREVA("INSERT INTO "SERVICES_TABLE "("
                        SERVICE_MULTIPLEXUID ","
                        SERVICE_ID ","
                        SERVICE_SOURCE ","
                        SERVICE_CA ","
                        SERVICE_TYPE ","
                        SERVICE_PMTVERSION ","
                        SERVICE_PMTPID ","
                        SERVICE_PCRPID ","
                        SERVICE_NAME ")"
                        "VALUES (%d,%d,%d,%d,%d,%d,%d,%d,'%q');",
                        uid, id, source, ca, type, pmtversion, pmtpid, pcrpid, name);
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
                        "WHERE " SERVICE_MULTIPLEXUID "=%d AND " SERVICE_ID "=%d;",
                        pmtversion, service->multiplexUID,  service->id);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();
    if (rc == SQLITE_DONE)
    {
        service->pmtVersion = pmtversion;
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
                        "WHERE " SERVICE_MULTIPLEXUID "=%d AND " SERVICE_ID "=%d;",
                        pmtpid, service->multiplexUID,  service->id);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();
    if (rc == SQLITE_DONE)
    {
        service->pmtPid = pmtpid;
        rc = SQLITE_OK;
    }
    else
    {
        PRINTLOG_SQLITE3ERROR();
    }
    STATEMENT_FINALIZE();
    return rc;
}

int ServicePCRPIDSet(Service_t  *service, int pcrpid)
{
    STATEMENT_INIT;

    STATEMENT_PREPAREVA("UPDATE " SERVICES_TABLE " "
                        "SET " SERVICE_PCRPID "=%d "
                        "WHERE " SERVICE_MULTIPLEXUID "=%d AND " SERVICE_ID "=%d;",
                        pcrpid, service->multiplexUID,  service->id);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();
    if (rc == SQLITE_DONE)
    {
        service->pcrPid = pcrpid;
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
                        "WHERE " SERVICE_MULTIPLEXUID "=%d AND " SERVICE_ID "=%d;",
                        name, service->multiplexUID,  service->id);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();
    if (rc == SQLITE_DONE)
    {
        if (name != service->name)
        {
            if (service->name)
            {
                free(service->name);
            }
            service->name = strdup(name);
        }
        rc = SQLITE_OK;
    }
    else
    {
        PRINTLOG_SQLITE3ERROR();
    }
    STATEMENT_FINALIZE();
    return rc;
}

int ServiceSourceSet(Service_t  *service, int source)
{
    STATEMENT_INIT;

    STATEMENT_PREPAREVA("UPDATE " SERVICES_TABLE " "
                        "SET " SERVICE_SOURCE "=%d "
                        "WHERE " SERVICE_MULTIPLEXUID "=%d AND " SERVICE_ID "=%d;",
                        source, service->multiplexUID,  service->id);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();
    if (rc == SQLITE_DONE)
    {
        service->source = source;
        rc = SQLITE_OK;
    }
    else
    {
        PRINTLOG_SQLITE3ERROR();
    }
    STATEMENT_FINALIZE();
    return rc;
}

int ServiceConditionalAccessSet(Service_t  *service, bool ca)
{
    STATEMENT_INIT;

    STATEMENT_PREPAREVA("UPDATE " SERVICES_TABLE " "
                        "SET " SERVICE_CA "=%d "
                        "WHERE " SERVICE_MULTIPLEXUID "=%d AND " SERVICE_ID "=%d;",
                        ca, service->multiplexUID,  service->id);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();
    if (rc == SQLITE_DONE)
    {
        service->conditionalAccess = ca;
        rc = SQLITE_OK;
    }
    else
    {
        PRINTLOG_SQLITE3ERROR();
    }
    STATEMENT_FINALIZE();
    return rc;
}

int ServiceTypeSet(Service_t  *service, ServiceType type)
{
    STATEMENT_INIT;

    STATEMENT_PREPAREVA("UPDATE " SERVICES_TABLE " "
                        "SET " SERVICE_TYPE "=%d "
                        "WHERE " SERVICE_MULTIPLEXUID "=%d AND " SERVICE_ID "=%d;",
                        type, service->multiplexUID,  service->id);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();
    if (rc == SQLITE_DONE)
    {
        service->type = type;
        rc = SQLITE_OK;
    }
    else
    {
        PRINTLOG_SQLITE3ERROR();
    }
    STATEMENT_FINALIZE();
    return rc;
}

int ServiceProviderSet(Service_t  *service, char *provider)
{
    STATEMENT_INIT;

    STATEMENT_PREPAREVA("UPDATE " SERVICES_TABLE " "
                        "SET " SERVICE_PROVIDER "='%q' "
                        "WHERE " SERVICE_MULTIPLEXUID "=%d AND " SERVICE_ID "=%d;",
                        provider, service->multiplexUID,  service->id);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();
    if (rc == SQLITE_DONE)
    {
        if (provider != service->provider)
        {
            if (service->provider)
            {
                free(service->provider);
            }
            service->provider = strdup(provider);
        }
        rc = SQLITE_OK;
    }
    else
    {
        PRINTLOG_SQLITE3ERROR();
    }
    STATEMENT_FINALIZE();
    return rc;
}

int ServiceDefaultAuthoritySet(Service_t  *service, char *defaultAuthority)
{
    STATEMENT_INIT;

    STATEMENT_PREPAREVA("UPDATE " SERVICES_TABLE " "
                        "SET " SERVICE_DEFAUTHORITY "='%q' "
                        "WHERE " SERVICE_MULTIPLEXUID "=%d AND " SERVICE_ID "=%d;",
                        defaultAuthority, service->multiplexUID,  service->id);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();
    if (rc == SQLITE_DONE)
    {
        if (defaultAuthority != service->defaultAuthority)
        {
            if (service->defaultAuthority)
            {
                free(service->defaultAuthority);
            }
            service->defaultAuthority = strdup(defaultAuthority);
        }
        rc = SQLITE_OK;
    }
    else
    {
        PRINTLOG_SQLITE3ERROR();
    }
    STATEMENT_FINALIZE();
    return rc;
}

Service_t *ServiceFind(char *name)
{
    Service_t *result = ServiceFindName( name);
    if (!result)
    {
        result = ServiceFindFQIDStr(name);
    }
    return result;
}

Service_t *ServiceFindName(char *name)
{
    STATEMENT_INIT;
    Service_t *result;

    STATEMENT_PREPAREVA("SELECT " SERVICE_FIELDS
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

    STATEMENT_PREPAREVA("SELECT " SERVICE_FIELDS
                        "FROM " SERVICES_TABLE " WHERE " SERVICE_MULTIPLEXUID "=%d AND " SERVICE_ID "=%d;",
                        multiplex->uid, id);
    RETURN_ON_ERROR(NULL);

    result = ServiceGetNext((ServiceEnumerator_t) stmt);
    STATEMENT_FINALIZE();
    return result;
}

Service_t *ServiceFindFQID(uint16_t networkId, uint16_t tsId, uint16_t serviceId)
{
    Service_t *service = NULL;
    Multiplex_t *multiplex;

    multiplex = MultiplexFindId(networkId, tsId);
    if (multiplex)
    {
        service = ServiceFindId(multiplex, serviceId);
        MultiplexRefDec(multiplex);
    }
    return service;
}

Service_t *ServiceFindFQIDStr(char *FQIdStr)
{
    uint16_t networkId = 0;
    uint16_t tsId = 0;
    uint16_t serviceId = 0;
    Service_t *service = NULL;
    
    if (sscanf(FQIdStr, "%hx.%hx.%hx", &networkId, &tsId, &serviceId) == 3)
    {
        service = ServiceFindFQID(networkId, tsId, serviceId);
    }
    return service;
}

ServiceEnumerator_t ServiceEnumeratorGet()
{
    STATEMENT_INIT;
    STATEMENT_PREPARE("SELECT "	SERVICE_FIELDS
                      "FROM " SERVICES_TABLE ";");
    RETURN_ON_ERROR(NULL);
    return stmt;
}

int ServiceForMultiplexCount(int uid)
{
    STATEMENT_INIT;
    int result = -1;

    STATEMENT_PREPAREVA("SELECT count() "
                        "FROM " SERVICES_TABLE " WHERE " SERVICE_MULTIPLEXUID "=%d;",
                        uid);
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

ServiceEnumerator_t ServiceEnumeratorForMultiplex(Multiplex_t *multiplex)
{
    STATEMENT_INIT;

    STATEMENT_PREPAREVA("SELECT " SERVICE_FIELDS
                        "FROM " SERVICES_TABLE " WHERE " SERVICE_MULTIPLEXUID"=%d;",
                        multiplex->uid);
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

        service = ServiceNew();
        service->multiplexUID = STATEMENT_COLUMN_INT( 0);
        service->id = STATEMENT_COLUMN_INT( 1);
        service->source = STATEMENT_COLUMN_INT( 2);
        service->conditionalAccess = STATEMENT_COLUMN_INT(3) ? TRUE:FALSE;
        service->type = STATEMENT_COLUMN_INT( 4);
        name = STATEMENT_COLUMN_TEXT( 5);
        if (name)
        {
            service->name = strdup(name);
        }
        service->pmtVersion = STATEMENT_COLUMN_INT( 6);
        service->pmtPid = STATEMENT_COLUMN_INT( 7);
        service->pcrPid = STATEMENT_COLUMN_INT( 8);
        name = STATEMENT_COLUMN_TEXT( 9);
        if (name)
        {
            service->provider = strdup(name);
        }
        name = STATEMENT_COLUMN_TEXT( 10);
        if (name)
        {
            service->defaultAuthority= strdup(name);
        }
        return service;
    }

    if (rc != SQLITE_DONE)
    {
        PRINTLOG_SQLITE3ERROR();
    }
    return NULL;
}

/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/
static void ServiceDestructor(void * arg)
{
    Service_t *service = arg;
    if (service->name)
    {
        free(service->name);
    }
    if (service->provider)
    {
        free(service->provider);
    }
    if (service->defaultAuthority)
    {
        free(service->defaultAuthority);
    }
}


