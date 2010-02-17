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
#include "events.h"
#include "yamlutils.h"

/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/

#define SERVICE_FIELDS SERVICES_TABLE "." SERVICE_MULTIPLEXUID "," \
                       SERVICES_TABLE "." SERVICE_ID "," \
                       SERVICES_TABLE "." SERVICE_SOURCE "," \
                       SERVICES_TABLE "." SERVICE_CA "," \
                       SERVICES_TABLE "." SERVICE_TYPE "," \
                       SERVICES_TABLE "." SERVICE_NAME "," \
                       SERVICES_TABLE "." SERVICE_PMTVERSION "," \
                       SERVICES_TABLE "." SERVICE_PMTPID ","\
                       SERVICES_TABLE "." SERVICE_PCRPID "," \
                       SERVICES_TABLE "." SERVICE_PROVIDER "," \
                       SERVICES_TABLE "." SERVICE_DEFAUTHORITY "," \
                       MULTIPLEXES_TABLE "." MULTIPLEX_NETID ","  \
                       MULTIPLEXES_TABLE "." MULTIPLEX_TSID " "

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/

static List_t *ServiceCreateList(ServiceEnumerator_t enumerator);
static ServiceList_t *ServiceGetList(char *where);
static void ServiceDestructor(void * arg);
static void ServiceListDestructor(void * arg);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/

static EventSource_t servicesSource;
static Event_t serviceAddedEvent;
static Event_t serviceDeletedEvent;
static Event_t serviceAllDeletedEvent;

static const char SERVICES[] = "Services";

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/

int ServiceInit(void)
{
    int result = 0;
    result = ObjectRegisterTypeDestructor(Service_t, ServiceDestructor);
    if (!result)
    {
        result = ObjectRegisterCollection(TOSTRING(ServiceList_t),sizeof(Service_t *),ServiceListDestructor);
    }
    if (!result)
    {
        servicesSource = EventsRegisterSource("Services");
        serviceAddedEvent = EventsRegisterEvent(servicesSource, "Added", ServiceEventToString);
        serviceDeletedEvent = EventsRegisterEvent(servicesSource, "Deleted", ServiceEventToString);
        serviceAllDeletedEvent = EventsRegisterEvent(servicesSource, "AllDeleted", MultiplexEventToString);
    }
    return  result;
}

int ServiceDeInit(void)
{
    EventsUnregisterSource(servicesSource);
    return 0;
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
    EventsFireEventListeners(serviceDeletedEvent, service);

    return 0;
}

int ServiceDeleteAll(Multiplex_t *mux)
{
    STATEMENT_INIT;

    STATEMENT_PREPAREVA("DELETE FROM " SERVICES_TABLE " "
                        "WHERE " SERVICE_MULTIPLEXUID "=%d;",
                        mux->uid);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();

    STATEMENT_FINALIZE();

    EventsFireEventListeners(serviceAllDeletedEvent, mux);
    return 0;
}

int ServiceAdd(int uid, char *name, int id, int source, bool ca, ServiceType type,
                    int pmtversion, int pmtpid, int pcrpid)
{
    Service_t *service = NULL;

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

    /* Create a service object to send in the event. */
    service = ServiceNew();
    service->multiplexUID = uid;
    service->id = id;
    service->name = strdup(name);
    service->source = source;
    service->conditionalAccess = ca;
    service->type = type;
    service->pmtVersion = pmtversion;
    service->pmtPid = pmtpid;
    service->pcrPid = pcrpid;
    EventsFireEventListeners(serviceAddedEvent, service);
    ServiceRefDec(service);

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
                        "FROM " SERVICES_TABLE "," MULTIPLEXES_TABLE " WHERE " SERVICES_TABLE "." SERVICE_NAME "='%q' AND " 
                        MULTIPLEXES_TABLE "." MULTIPLEX_UID "=" SERVICES_TABLE "." SERVICE_MULTIPLEXUID ";",
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
                        "FROM " SERVICES_TABLE "," MULTIPLEXES_TABLE " WHERE " SERVICES_TABLE "." SERVICE_MULTIPLEXUID "=%d AND " 
                        SERVICES_TABLE "." SERVICE_ID "=%d AND " 
                        MULTIPLEXES_TABLE "." MULTIPLEX_UID "=" SERVICES_TABLE "." SERVICE_MULTIPLEXUID ";",
                        multiplex->uid, id);
    RETURN_ON_ERROR(NULL);

    result = ServiceGetNext((ServiceEnumerator_t) stmt);
    STATEMENT_FINALIZE();
    return result;
}

Service_t *ServiceFindFQID(uint16_t networkId, uint16_t tsId, uint16_t serviceId)
{
    STATEMENT_INIT;
    Service_t *result;

    STATEMENT_PREPAREVA("SELECT " SERVICE_FIELDS
                        "FROM " SERVICES_TABLE "," MULTIPLEXES_TABLE " WHERE "
                        MULTIPLEXES_TABLE "." MULTIPLEX_NETID "=%d AND "
                        MULTIPLEXES_TABLE "." MULTIPLEX_TSID "=%d AND "
                        SERVICES_TABLE "." SERVICE_MULTIPLEXUID "=" MULTIPLEXES_TABLE "." MULTIPLEX_UID " AND "
                        SERVICE_ID "=%d;",
                        networkId, tsId, serviceId);
    RETURN_ON_ERROR(NULL);

    result = ServiceGetNext((ServiceEnumerator_t) stmt);

    STATEMENT_FINALIZE();
    return result;
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
    STATEMENT_PREPARE("SELECT " SERVICE_FIELDS
                      "FROM " SERVICES_TABLE"," MULTIPLEXES_TABLE " WHERE " 
                      SERVICES_TABLE "." SERVICE_MULTIPLEXUID "=" MULTIPLEXES_TABLE "." MULTIPLEX_UID ";");
    RETURN_ON_ERROR(NULL);
    return stmt;
}

List_t *ServiceListAll()
{
    return ServiceCreateList(ServiceEnumeratorGet());
}

ServiceList_t *ServiceGetAll()
{
    return ServiceGetList(NULL);
}

ServiceEnumerator_t ServiceEnumeratorForMultiplex(Multiplex_t *multiplex)
{
    STATEMENT_INIT;

    STATEMENT_PREPAREVA("SELECT " SERVICE_FIELDS
                        "FROM " SERVICES_TABLE"," MULTIPLEXES_TABLE " WHERE " 
                        SERVICES_TABLE "." SERVICE_MULTIPLEXUID "=" MULTIPLEXES_TABLE "." MULTIPLEX_UID " AND "
                        SERVICES_TABLE "."SERVICE_MULTIPLEXUID"=%d;",
                         multiplex->uid);
    RETURN_ON_ERROR(NULL);

    return stmt;
}

List_t *ServiceListForMultiplex(Multiplex_t *multiplex)
{
    return ServiceCreateList(ServiceEnumeratorForMultiplex(multiplex));
}

ServiceList_t *ServiceGetListForMultiplex(Multiplex_t *multiplex)
{
    char where[50];
    sprintf(where, SERVICES_TABLE "." SERVICE_MULTIPLEXUID"=%d", multiplex->uid);
    return ServiceGetList(where);
}

ServiceEnumerator_t ServiceFindByPID(int pid, Multiplex_t *multiplex)
{
    STATEMENT_INIT;
    char * multiplexClause;

    if (multiplex)
    {
        multiplexClause = sqlite3_mprintf("AND " SERVICES_TABLE "."
                                       SERVICE_MULTIPLEXUID "=%d "
                                       "AND " PIDS_TABLE "."
                                       PID_MULTIPLEXUID "=%d ",
                                       multiplex->uid, multiplex->uid);
    }
    else
    {
        multiplexClause = sqlite3_mprintf("AND " SERVICES_TABLE "."
                                       SERVICE_MULTIPLEXUID "="
                                       PIDS_TABLE "." PID_MULTIPLEXUID);
    }

    if (!multiplexClause)
    {
        return NULL;
    }



    STATEMENT_PREPAREVA("SELECT DISTINCT " SERVICE_FIELDS
                        "FROM " SERVICES_TABLE "," MULTIPLEXES_TABLE "," PIDS_TABLE " "
                        "WHERE " SERVICE_ID "=" PID_SERVICEID " %s AND "
                        SERVICES_TABLE "." SERVICE_MULTIPLEXUID "=" MULTIPLEXES_TABLE "." MULTIPLEX_UID " AND "
                        "(" PIDS_TABLE "." PID_PID "=%d "
                        "OR " SERVICES_TABLE "." SERVICE_PMTPID "=%d "
                        "OR " SERVICES_TABLE "." SERVICE_PCRPID "=%d );",
                        multiplexClause, pid, pid, pid);
    sqlite3_free(multiplexClause);

    RETURN_ON_ERROR(NULL);

    return stmt;
}

List_t *ServiceListForPID(int pid, Multiplex_t *multiplex)
{
    return ServiceCreateList(ServiceFindByPID(pid, multiplex));
}


ServiceEnumerator_t ServiceQueryNameLike(char *query)
{
    STATEMENT_INIT;

    STATEMENT_PREPAREVA("SELECT " SERVICE_FIELDS
                        "FROM " SERVICES_TABLE"," MULTIPLEXES_TABLE " WHERE " 
                        SERVICES_TABLE "." SERVICE_MULTIPLEXUID "=" MULTIPLEXES_TABLE "." MULTIPLEX_UID " AND " 
                        SERVICES_TABLE "." SERVICE_NAME " LIKE %Q;",
                        query);
    RETURN_ON_ERROR(NULL);

    return stmt;
}

List_t *ServiceListForNameLike(char *query)
{
    return ServiceCreateList(ServiceQueryNameLike(query));
}

ServiceList_t *ServiceGetListForNameLike(char *query)
{
    char *where;
    ServiceList_t *list;
    
    where = sqlite3_mprintf(SERVICES_TABLE "." SERVICE_NAME " LIKE %Q", query);
    list = ServiceGetList(where);
    sqlite3_free(where);
    return list;
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
        service->id = STATEMENT_COLUMN_INT( 1) & 0xffff;
        service->source = STATEMENT_COLUMN_INT( 2);
        service->conditionalAccess = STATEMENT_COLUMN_INT(3) ? TRUE:FALSE;
        service->type = STATEMENT_COLUMN_INT( 4);
        name = STATEMENT_COLUMN_TEXT( 5);
        if (name)
        {
            service->name = strdup(name);
        }
        service->pmtVersion = STATEMENT_COLUMN_INT( 6);
        service->pmtPid = STATEMENT_COLUMN_INT( 7) & 0xffff;
        service->pcrPid = STATEMENT_COLUMN_INT( 8) & 0xffff;
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
        service->networkId = STATEMENT_COLUMN_INT(11) & 0xffff;
        service->tsId =  STATEMENT_COLUMN_INT(12) & 0xffff;
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
static List_t *ServiceCreateList(ServiceEnumerator_t enumerator)
{
    List_t *list = ObjectListCreate();
    Service_t *service = NULL;

    do
    {
        service = ServiceGetNext(enumerator);
        if (service)
        {
            ListAdd(list, service);
        }
    }
    while (service != NULL);
    
    ServiceEnumeratorDestroy(enumerator);
    return list;
}

static ServiceList_t *ServiceGetList(char *where)
{
    int count, i;
    ServiceList_t *list;    
    STATEMENT_INIT;
    count = DBaseCount(SERVICES_TABLE, where);
    if (where)
    {
        STATEMENT_PREPAREVA("SELECT " SERVICE_FIELDS
                        "FROM " SERVICES_TABLE"," MULTIPLEXES_TABLE " WHERE " 
                        SERVICES_TABLE "." SERVICE_MULTIPLEXUID "=" MULTIPLEXES_TABLE "." MULTIPLEX_UID " AND %s;", where);
    }
    else
    {
        STATEMENT_PREPARE("SELECT " SERVICE_FIELDS
                          "FROM " SERVICES_TABLE"," MULTIPLEXES_TABLE " WHERE " 
                          SERVICES_TABLE "." SERVICE_MULTIPLEXUID "=" MULTIPLEXES_TABLE "." MULTIPLEX_UID ";");

    }
    list = (ServiceList_t*)ObjectCollectionCreate(TOSTRING(ServiceList_t), count);
    if (list)
    {
        for (i = 0; i < count; i ++)
        {
            list->services[i] = ServiceGetNext((ServiceEnumerator_t)stmt);
        }
    }
    STATEMENT_FINALIZE();
    return list;
}

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

static void ServiceListDestructor(void * arg)
{
    ServiceList_t *list = arg;
    int i;
    for (i = 0;i < list->nrofServices; i ++)
    {
        ObjectRefDec(list->services[i]);
    }
}


int ServiceEventToString(yaml_document_t *document, Event_t event, void * payload)
{
    Service_t *service = payload;
    char idStr[20] = {0};
    char *name = idStr;
    int mappingId = yaml_document_add_mapping(document, (yaml_char_t*)YAML_MAP_TAG, YAML_ANY_MAPPING_STYLE);
    if (service)
    {
        sprintf(idStr, "%04x.%04x.%04x", service->networkId & 0xffff, service->tsId & 0xffff, service->id & 0xffff);
        name = service->name;
    }
    YamlUtils_MappingAdd(document, mappingId, "Service ID", idStr);
    YamlUtils_MappingAdd(document, mappingId, "Service Name", name);
    return mappingId;
}

