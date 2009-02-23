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

cache.c

Caches service and PID information from the database for the current multiplex.

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "ts.h"
#include "multiplexes.h"
#include "services.h"
#include "pids.h"
#include "logging.h"
#include "cache.h"
#include "dbase.h"
#include "main.h"
#include "messageq.h"

/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/

#define SERVICES_MAX (256)

/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/

enum CacheFlags
{
    CacheFlag_Clean           = 0x0000,
    CacheFlag_Dirty_PMTPID    = 0x0001,
    CacheFlag_Dirty_PIDs      = 0x0002, /* Also means PMT Version and PCR PID needs to be updated */
    CacheFlag_Dirty_Name      = 0x0004,
    CacheFlag_Dirty_Source    = 0x0008,
    CacheFlag_Dirty_CA        = 0x0010,
    CacheFlag_Dirty_Type      = 0x0020,
    CacheFlag_Dirty_Provider  = 0x0040,
    CacheFlag_Dirty_DefAuth   = 0x0060,
    CacheFlag_Not_Seen_In_SDT = 0x2000,
    CacheFlag_Not_Seen_In_PAT = 0x4000,
    CacheFlag_Dirty_Added     = 0x8000,
};

enum CacheUpdateType
{
    CacheUpdate_Multiplex_PAT_Version_TS_id,
    CacheUpdate_Multiplex_Network_id,
    CacheUpdate_Service_PMT_PID,
    CacheUpdate_Service_PIDs,
    CacheUpdate_Service_Name,
    CacheUpdate_Service_Source,
    CacheUpdate_Service_CA,
    CacheUpdate_Service_Type,
    CacheUpdate_Service_Provider,
    CacheUpdate_Service_Default_Auth,
    CacheUpdate_Service_Added,
    CacheUpdate_Service_Deleted,
};


typedef struct CacheUpdateMessage_s
{
    enum CacheUpdateType type;
    union
    {
        struct
        {
            Multiplex_t *multiplex;
            int patVersion;
            int tsId;
        }multiplexPATVersionTSId;

        struct
        {
            Multiplex_t *multiplex;
            int networkId;
        }multiplexNetworkId;

        struct
        {
            Service_t *service;
            int pmtPid;
        }servicePMTPID;

        struct
        {
            Service_t *service;
            PIDList_t *pids;
            int pcrPid;
            int pmtVersion;
        }servicePIDs;

        struct
        {
            Service_t *service;
            char *name;
        }serviceName;

        struct
        {
            Service_t *service;
            uint16_t source;
        }serviceSource;

        struct
        {
            Service_t *service;
            bool ca;
        }serviceCA;

        struct
        {
            Service_t *service;
            ServiceType type;
        }serviceType;

        struct
        {
            Service_t *service;
            char *provider;
        }serviceProvider;

        struct
        {
            Service_t *service;
            char *defaultAuthority;
        }serviceDefaultAuthority;

        struct
        {
            int id;
            int source;
            int multiplexUID;
            char *name;
        }serviceAdd;

        struct
        {
            Service_t *service;
        }serviceDelete;
    }details;
}CacheUpdateMessage_t;

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void CacheServicesFree(void);
static void *CacheUpdateProcessor(void *arg);
static void CacheProcessUpdateMessage(CacheUpdateMessage_t *msg);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static char CACHE[] = "Cache";
static Multiplex_t *cachedServicesMultiplex = NULL;
static int cachedServicesCount = 0;

static pthread_mutex_t cacheUpdateMutex;
static enum CacheFlags cacheFlags[SERVICES_MAX];
static Service_t*      cachedServices[SERVICES_MAX];
static PIDList_t*      cachedPIDs[SERVICES_MAX];

static MessageQ_t cacheUpdateQ;
static pthread_t  cacheUpdateThread;

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/

int CacheInit()
{
    pthread_mutex_init(&cacheUpdateMutex, NULL);
    cacheUpdateQ = MessageQCreate();
    ObjectRegisterType(CacheUpdateMessage_t);
    pthread_create(&cacheUpdateThread, NULL, CacheUpdateProcessor, NULL);
    return 0;
}

void CacheDeInit()
{
    /* Send quit signal to update thread */
    MessageQSetQuit(cacheUpdateQ);
    pthread_join(cacheUpdateThread, NULL);
    MessageQDestroy(cacheUpdateQ);

    CacheServicesFree();
    pthread_mutex_destroy(&cacheUpdateMutex);
    pthread_detach(cacheUpdateThread);

}

int CacheLoad(Multiplex_t *multiplex)
{
    int result = 1;
    List_t *list = NULL;

    pthread_mutex_lock(&cacheUpdateMutex);
    LogModule(LOG_DEBUG, CACHE, "Freeing services\n");

    /* Free the services and PIDs from the previous multiplex */
    CacheServicesFree();

    list = ServiceListForMultiplex(multiplex);
    
    LogModule(LOG_DEBUG, CACHE, "Loading %d services for %d\n", ListCount(list), multiplex->uid);
    if (ListCount(list) > 0)
    {
        ListIterator_t iterator;
        int i = 0;
        
        for (ListIterator_Init(iterator, list); 
             ListIterator_MoreEntries(iterator);
             ListIterator_Next(iterator), i++)
        {
            cachedServices[i] = (Service_t*)ListIterator_Current(iterator);
            LogModule(LOG_DEBUG,CACHE, "Loaded 0x%04x %s\n", cachedServices[i]->id, cachedServices[i]->name);
            cachedPIDs[i] = PIDListGet(cachedServices[i]);
            cacheFlags[i] = CacheFlag_Clean;
        }
        /* Use ListFree with no destructor as we don't want to free the objects 
         * only the list.
         */
        ListFree(list, NULL);
    }

    cachedServicesCount = ListCount(list);

    MultiplexRefInc(multiplex);
    cachedServicesMultiplex = multiplex;
    result = 0;

    pthread_mutex_unlock(&cacheUpdateMutex);

    return result;
}

Multiplex_t *CacheMultiplexGet(void)
{
     return cachedServicesMultiplex;
}

Service_t *CacheServiceFind(char *name)
{
    Service_t *result = NULL;
    int netId;
    int tsId;
    int serviceId;

    result = CacheServiceFindName(name);
    if (!result)
    {
        if (sscanf(name,"%x.%x.%x", &netId, &tsId, &serviceId) == 3)
        {
            if ((cachedServicesMultiplex->networkId == netId) &&
                (cachedServicesMultiplex->tsId== tsId))
            {
                result = CacheServiceFindId(serviceId);
            }
        }
    }
    return result;
}

Service_t *CacheServiceFindId(int id)
{
    Service_t *result = NULL;
    int i;

    for (i = 0; i < cachedServicesCount; i ++)
    {
        if (cachedServices[i]->id == id)
        {
            result = cachedServices[i];
            ServiceRefInc(result);
            break;
        }
    }

    return result;
}

Service_t *CacheServiceFindName(char *name)
{
    Service_t *result = NULL;
    int i;
    LogModule(LOG_DEBUGV,CACHE, "Checking cached services for \"%s\"\n", name);
    for (i = 0; i < cachedServicesCount; i ++)
    {
        LogModule(LOG_DEBUGV, CACHE, "cachedServices[%d]->name = %s\n", i, cachedServices[i]->name);
        if (strcmp(cachedServices[i]->name, name) == 0)
        {
            result = cachedServices[i];
            ServiceRefInc(result);
            LogModule(LOG_DEBUGV, CACHE, "Found in cached services!\n");
            break;
        }
    }

    if (result == NULL)
    {
        LogModule(LOG_DEBUGV, CACHE, "Not found in cached services\n");
    }
    return result;
}

Service_t **CacheServicesGet(int *count)
{
    pthread_mutex_lock(&cacheUpdateMutex);
    *count = cachedServicesCount;
    return cachedServices;
}

void CacheServicesRelease(void)
{
    pthread_mutex_unlock(&cacheUpdateMutex);
}

PIDList_t *CachePIDsGet(Service_t *service)
{
    PIDList_t *result = NULL;
    int i;
    pthread_mutex_lock(&cacheUpdateMutex);
    for (i = 0; i < cachedServicesCount; i ++)
    {
        if ((cachedServices[i]) && ServiceAreEqual(service, cachedServices[i]))
        {
            result = cachedPIDs[i];
            break;
        }
    }
    if (!result)
    {
        pthread_mutex_unlock(&cacheUpdateMutex);
    }
    return result;
}

void CachePIDsRelease(void)
{
    pthread_mutex_unlock(&cacheUpdateMutex);
}

void CacheUpdateMultiplex(Multiplex_t *multiplex, int patversion, int tsid)
{
    CacheUpdateMessage_t *msg;
    pthread_mutex_lock(&cacheUpdateMutex);

    if (cachedServicesMultiplex && MultiplexAreEqual(multiplex, cachedServicesMultiplex))
    {
        cachedServicesMultiplex->patVersion = patversion;
        cachedServicesMultiplex->tsId = tsid;
        msg = ObjectCreateType(CacheUpdateMessage_t);
        if (msg)
        {
            msg->type = CacheUpdate_Multiplex_PAT_Version_TS_id;
            ObjectRefInc(multiplex);
            msg->details.multiplexPATVersionTSId.multiplex = multiplex;
            msg->details.multiplexPATVersionTSId.patVersion = patversion;
            msg->details.multiplexPATVersionTSId.tsId = tsid;
            MessageQSend(cacheUpdateQ, msg);
            ObjectRefDec(msg);
        }
    }

    pthread_mutex_unlock(&cacheUpdateMutex);
}

void CacheUpdateNetworkId(Multiplex_t *multiplex, int netid)
{
    CacheUpdateMessage_t *msg;
    pthread_mutex_lock(&cacheUpdateMutex);

    if (cachedServicesMultiplex && MultiplexAreEqual(multiplex, cachedServicesMultiplex))
    {
        cachedServicesMultiplex->networkId = netid;
        msg = ObjectCreateType(CacheUpdateMessage_t);
        if (msg)
        {
            msg->type = CacheUpdate_Multiplex_Network_id;
            ObjectRefInc(multiplex);
            msg->details.multiplexNetworkId.multiplex = multiplex;
            msg->details.multiplexNetworkId.networkId = netid;
            MessageQSend(cacheUpdateQ, msg);
            ObjectRefDec(msg);
        }
    }

    pthread_mutex_unlock(&cacheUpdateMutex);
}

void CacheUpdateServicePMTPID(Service_t *service, int pmtpid)
{
    CacheUpdateMessage_t *msg;
    int i;
    pthread_mutex_lock(&cacheUpdateMutex);

    for (i = 0; i < cachedServicesCount; i ++)
    {
        if ((cachedServices[i]) && ServiceAreEqual(service, cachedServices[i]))
        {
            cachedServices[i]->pmtPid = pmtpid;
            msg = ObjectCreateType(CacheUpdateMessage_t);
            if (msg)
            {
                msg->type = CacheUpdate_Service_PMT_PID;
                ObjectRefInc(service);
                msg->details.servicePMTPID.service = service;
                msg->details.servicePMTPID.pmtPid = pmtpid;
                MessageQSend(cacheUpdateQ, msg);
                ObjectRefDec(msg);
            }
            break;
        }
    }

    pthread_mutex_unlock(&cacheUpdateMutex);
}

void CacheUpdateServiceName(Service_t *service, char *name)
{
    CacheUpdateMessage_t *msg;
    int i;
    pthread_mutex_lock(&cacheUpdateMutex);

    for (i = 0; i < cachedServicesCount; i ++)
    {
        if ((cachedServices[i]) && ServiceAreEqual(service, cachedServices[i]))
        {
            if (cachedServices[i]->name)
            {
                free(cachedServices[i]->name);
            }
            if (name)
            {
                cachedServices[i]->name = strdup(name);
            }
            else
            {
                cachedServices[i]->name = NULL;
            }
            msg = ObjectCreateType(CacheUpdateMessage_t);
            if (msg)
            {
                msg->type = CacheUpdate_Service_Name;
                ObjectRefInc(service);
                msg->details.serviceName.service = service;
                if (name)
                {
                    msg->details.serviceName.name = strdup(name);
                }
                MessageQSend(cacheUpdateQ, msg);
                ObjectRefDec(msg);
            }
            break;
        }
    }

    pthread_mutex_unlock(&cacheUpdateMutex);
}

void CacheUpdateServiceProvider(Service_t *service, char *provider)
{
    CacheUpdateMessage_t *msg;
    int i;
    pthread_mutex_lock(&cacheUpdateMutex);

    for (i = 0; i < cachedServicesCount; i ++)
    {
        if ((cachedServices[i]) && ServiceAreEqual(service, cachedServices[i]))
        {
            if (cachedServices[i]->provider)
            {
                free(cachedServices[i]->provider);
            }
            if (provider)
            {
                cachedServices[i]->provider = strdup(provider);
            }
            else
            {
                cachedServices[i]->provider = NULL;
            }
            msg = ObjectCreateType(CacheUpdateMessage_t);
            if (msg)
            {
                msg->type = CacheUpdate_Service_Provider;
                ObjectRefInc(service);
                msg->details.serviceProvider.service = service;
                if (provider)
                {
                    msg->details.serviceProvider.provider = strdup(provider);
                }
                MessageQSend(cacheUpdateQ, msg);
                ObjectRefDec(msg);
            }
            break;
        }
    }

    pthread_mutex_unlock(&cacheUpdateMutex);
}

void CacheUpdateServiceDefaultAuthority(Service_t *service, char *defaultAuthority)
{
    CacheUpdateMessage_t *msg;
    int i;
    pthread_mutex_lock(&cacheUpdateMutex);

    for (i = 0; i < cachedServicesCount; i ++)
    {
        if ((cachedServices[i]) && ServiceAreEqual(service, cachedServices[i]))
        {
            if (cachedServices[i]->defaultAuthority)
            {
                free(cachedServices[i]->defaultAuthority);
            }
            if (defaultAuthority)
            {
                cachedServices[i]->defaultAuthority = strdup(defaultAuthority);
            }
            else
            {
                cachedServices[i]->defaultAuthority = NULL;
            }
            msg = ObjectCreateType(CacheUpdateMessage_t);
            if (msg)
            {
                msg->type = CacheUpdate_Service_Default_Auth;
                ObjectRefInc(service);
                msg->details.serviceDefaultAuthority.service = service;
                if (defaultAuthority)
                {
                    msg->details.serviceDefaultAuthority.defaultAuthority = strdup(defaultAuthority);
                }
                MessageQSend(cacheUpdateQ, msg);
                ObjectRefDec(msg);
            }
            break;
        }
    }

    pthread_mutex_unlock(&cacheUpdateMutex);
}

void CacheUpdateServiceSource(Service_t *service, uint16_t source)
{
    CacheUpdateMessage_t *msg;
    int i;
    pthread_mutex_lock(&cacheUpdateMutex);

    for (i = 0; i < cachedServicesCount; i ++)
    {
        if ((cachedServices[i]) && ServiceAreEqual(service, cachedServices[i]))
        {
            cachedServices[i]->source = source;
            msg = ObjectCreateType(CacheUpdateMessage_t);
            if (msg)
            {
                msg->type = CacheUpdate_Service_Source;
                ObjectRefInc(service);
                msg->details.serviceSource.service = service;
                msg->details.serviceSource.source = source;
                MessageQSend(cacheUpdateQ, msg);
                ObjectRefDec(msg);
            }
            break;
        }
    }

    pthread_mutex_unlock(&cacheUpdateMutex);
}

void CacheUpdateServiceConditionalAccess(Service_t *service, bool ca)
{
    CacheUpdateMessage_t *msg;
    int i;
    pthread_mutex_lock(&cacheUpdateMutex);

    for (i = 0; i < cachedServicesCount; i ++)
    {
        if ((cachedServices[i]) && ServiceAreEqual(service, cachedServices[i]))
        {
            cachedServices[i]->conditionalAccess = ca;
            msg = ObjectCreateType(CacheUpdateMessage_t);
            if (msg)
            {
                msg->type = CacheUpdate_Service_CA;
                ObjectRefInc(service);
                msg->details.serviceCA.service = service;
                msg->details.serviceCA.ca = ca;
                MessageQSend(cacheUpdateQ, msg);
                ObjectRefDec(msg);
            }
            break;
        }
    }

    pthread_mutex_unlock(&cacheUpdateMutex);
}

void CacheUpdateServiceType(Service_t *service, ServiceType type)
{
    CacheUpdateMessage_t *msg;
    int i;
    pthread_mutex_lock(&cacheUpdateMutex);

    for (i = 0; i < cachedServicesCount; i ++)
    {
        if ((cachedServices[i]) && ServiceAreEqual(service, cachedServices[i]))
        {
            cachedServices[i]->type = type;
            msg = ObjectCreateType(CacheUpdateMessage_t);
            if (msg)
            {
                msg->type = CacheUpdate_Service_Type;
                ObjectRefInc(service);
                msg->details.serviceType.service = service;
                msg->details.serviceType.type = type;
                MessageQSend(cacheUpdateQ, msg);
                ObjectRefDec(msg);
            }
            break;
        }
    }

    pthread_mutex_unlock(&cacheUpdateMutex);
}

void CacheUpdatePIDs(Service_t *service, int pcrpid, PIDList_t *pids, int pmtversion)
{
    CacheUpdateMessage_t *msg;
    int i;
    pthread_mutex_lock(&cacheUpdateMutex);

    for (i = 0; i < cachedServicesCount; i ++)
    {
        if ((cachedServices[i]) && ServiceAreEqual(service, cachedServices[i]))
        {
            if (cachedPIDs[i])
            {
                PIDListFree(cachedPIDs[i]);
            }

            cachedPIDs[i] = pids;
            cachedServices[i]->pcrPid = pcrpid;
            cachedServices[i]->pmtVersion = pmtversion;

            msg = ObjectCreateType(CacheUpdateMessage_t);
            if (msg)
            {
                msg->type = CacheUpdate_Service_PIDs;
                ObjectRefInc(service);
                msg->details.servicePIDs.service = service;
                msg->details.servicePIDs.pids = PIDListClone(pids);
                msg->details.servicePIDs.pcrPid = pcrpid;
                msg->details.servicePIDs.pmtVersion = pmtversion;
                MessageQSend(cacheUpdateQ, msg);
                ObjectRefDec(msg);
            }
            break;
        }
    }

    pthread_mutex_unlock(&cacheUpdateMutex);
}

Service_t *CacheServiceAdd(int id)
{
    CacheUpdateMessage_t *msg;
    Service_t *result = ServiceNew();
    if (result)
    {
        result->id = id;
        if (MainIsDVB())
        {
            result->source = id;
        }
        else
        {
            result->source = -1;
        }
        result->pmtVersion = -1;
        result->pmtPid = 8192;
        if (asprintf(&result->name, "%04x", id) == -1)
        {
            LogModule(LOG_ERROR, CACHE, "Failed to allocate memory for default service name (0x%04x).\n", result->id);
            result->name = NULL;
        }
        result->multiplexUID = cachedServicesMultiplex->uid;

        pthread_mutex_lock(&cacheUpdateMutex);

        LogModule(LOG_DEBUG, CACHE, "Added service %04x at %d\n", result->id, cachedServicesCount);
        ServiceRefInc(result);
        cachedServices[cachedServicesCount] = result;
        cachedPIDs[cachedServicesCount] = NULL;
        cacheFlags[cachedServicesCount] = CacheFlag_Clean;
        cachedServicesCount ++;

        msg = ObjectCreateType(CacheUpdateMessage_t);
        if (msg)
        {
            msg->type = CacheUpdate_Service_Added;
            msg->details.serviceAdd.id = id;
            msg->details.serviceAdd.multiplexUID = cachedServicesMultiplex->uid;
            msg->details.serviceAdd.source = result->source;
            msg->details.serviceAdd.name = strdup(result->name);
            MessageQSend(cacheUpdateQ, msg);
            ObjectRefDec(msg);
        }

        pthread_mutex_unlock(&cacheUpdateMutex);
    }
    return result;
}

bool CacheServiceSeen(Service_t *service, bool seen, bool pat)
{
    bool exists = FALSE;
    int seenIndex = -1;
    int i;

    for (i = 0; i < cachedServicesCount; i ++)
    {
        if ((cachedServices[i]) && ServiceAreEqual(service, cachedServices[i]))
        {
            seenIndex = i;
            break;
        }
    }
    if (seenIndex != -1)
    {
        int flag = pat ? CacheFlag_Not_Seen_In_PAT : CacheFlag_Not_Seen_In_SDT;
        if (seen)
        {
            cacheFlags[seenIndex] &= ~flag;
        }
        else
        {
            cacheFlags[seenIndex] |= flag;
        }

        if ((cacheFlags[seenIndex] & CacheFlag_Not_Seen_In_PAT) &&
            (cacheFlags[seenIndex] & CacheFlag_Not_Seen_In_SDT))
        {
            exists = FALSE;
        }
        else
        {
            exists = TRUE;
        }
    }
    return exists;
}

void CacheServiceDelete(Service_t *service)
{
    CacheUpdateMessage_t *msg;
    int deletedIndex = -1;
    int i;
    pthread_mutex_lock(&cacheUpdateMutex);

    for (i = 0; i < cachedServicesCount; i ++)
    {
        if ((cachedServices[i]) && ServiceAreEqual(service, cachedServices[i]))
        {
            deletedIndex = i;
            break;
        }
    }

    if (deletedIndex != -1)
    {
        LogModule(LOG_DEBUG, CACHE, "Removing service at index %d\n", deletedIndex);
        /* Get rid of the pids as we don't need them any more! */
        PIDListFree(cachedPIDs[deletedIndex]);

        cachedServicesCount --;
        /* Remove the deleted service from the list */
        for (i = deletedIndex; i < cachedServicesCount; i ++)
        {
            LogModule(LOG_DEBUG, CACHE, "Moving %s (%x) to %d\n", cachedServices[i + 1]->name, cachedServices[i + 1]->id, i);
            cachedPIDs[i] = cachedPIDs[i + 1];
            cachedServices[i] = cachedServices[i + 1];
            cacheFlags[i] = cacheFlags [i + 1];
        }

        msg = ObjectCreateType(CacheUpdateMessage_t);
        if (msg)
        {
            msg->type = CacheUpdate_Service_Deleted;
            ObjectRefInc(service);
            msg->details.serviceDelete.service = service;
            MessageQSend(cacheUpdateQ, msg);
            ObjectRefDec(msg);
        }
    }

    pthread_mutex_unlock(&cacheUpdateMutex);
}

void CacheWriteback()
{
  /* Do nothing, move to CacheUpdateProcessor. */
}

static void CacheServicesFree()
{
    int i;
    for (i = 0; i < cachedServicesCount; i ++)
    {
        if (cachedServices[i])
        {
            ServiceRefDec(cachedServices[i]);
            cachedServices[i] = NULL;
        }
        if (cachedPIDs[i])
        {
            PIDListFree(cachedPIDs[i]);
            cachedPIDs[i] = NULL;
        }
    }
    cachedServicesCount = 0;
    MultiplexRefDec(cachedServicesMultiplex);
    cachedServicesMultiplex = NULL;
}

static void *CacheUpdateProcessor(void *arg)
{
    CacheUpdateMessage_t *msg;
    LogModule(LOG_DEBUG, CACHE, "Cache Update thread started.\n");
    while(!MessageQIsQuitSet(cacheUpdateQ))
    {
        msg = MessageQReceive(cacheUpdateQ);
        if (msg)
        {
            CacheProcessUpdateMessage(msg);
        }
    }

    MessageQResetQuit(cacheUpdateQ);
    while(MessageQAvailable(cacheUpdateQ))
    {
        msg = MessageQReceive(cacheUpdateQ);
        CacheProcessUpdateMessage(msg);
    }
    LogModule(LOG_DEBUG, CACHE, "Cache Update thread finished.\n");
    return NULL;
}

static void CacheProcessUpdateMessage(CacheUpdateMessage_t *msg)
{
    int rc;
    Multiplex_t *mux = NULL;
    Service_t *service = NULL;

    DBaseTransactionBegin();
    switch(msg->type)
    {
        case CacheUpdate_Multiplex_PAT_Version_TS_id:
            LogModule(LOG_DEBUG, CACHE, "Updating Multiplex PAT version and TS id\n");
            mux = msg->details.multiplexPATVersionTSId.multiplex;
            rc =MultiplexPATVersionSet(mux, msg->details.multiplexPATVersionTSId.patVersion);
            if (rc)
            {
                LogModule(LOG_ERROR, CACHE, "Failed to update Multiplex PAT version (0x%x)\n", rc);
            }
            rc = MultiplexTSIdSet(mux, msg->details.multiplexPATVersionTSId.tsId);
            if (rc)
            {
                LogModule(LOG_ERROR, CACHE, "Failed to update Multiplex TS id (0x%x)\n", rc);
            }
            break;

        case CacheUpdate_Multiplex_Network_id:
            LogModule(LOG_DEBUG, CACHE, "Updating Multiplex Original Network id\n");
            mux = msg->details.multiplexNetworkId.multiplex;
            rc = MultiplexNetworkIdSet(mux, msg->details.multiplexNetworkId.networkId);
            if (rc)
            {
                LogModule(LOG_ERROR, CACHE, "Failed to update Multiplex Original Network id (0x%x)\n", rc);
            }
            break;

        case CacheUpdate_Service_PMT_PID:
            service = msg->details.servicePMTPID.service;
            LogModule(LOG_DEBUG, CACHE, "Updating PMT PID for %s\n", service->name);
            ServicePMTPIDSet(service,  msg->details.servicePMTPID.pmtPid);
            break;

        case CacheUpdate_Service_PIDs:
            service = msg->details.servicePIDs.service;
            LogModule(LOG_DEBUG, CACHE, "Updating PIDs for %s\n", service->name);
            PIDListRemove(service);
            PIDListSet(service, msg->details.servicePIDs.pids);
            ServicePMTVersionSet(service, msg->details.servicePIDs.pmtVersion);
            ServicePCRPIDSet(service, msg->details.servicePIDs.pcrPid);
            PIDListFree(msg->details.servicePIDs.pids);
            break;

        case CacheUpdate_Service_Name:
            service = msg->details.serviceName.service;
            LogModule(LOG_DEBUG, CACHE, "Updating name for 0x%04x new name %s\n",
                service->id, msg->details.serviceName.name);
            ServiceNameSet(service, msg->details.serviceName.name);
            free(msg->details.serviceName.name);
            break;

        case CacheUpdate_Service_Source:
            service = msg->details.serviceSource.service;
            LogModule(LOG_DEBUG, CACHE, "Updating source for 0x%04x new source %x\n",
                service->id, msg->details.serviceSource.source);
            ServiceSourceSet(service, msg->details.serviceSource.source);
            break;

        case CacheUpdate_Service_CA:
            service = msg->details.serviceCA.service;
            LogModule(LOG_DEBUG, CACHE, "Updating CA state for 0x%04x new CA state %s\n",
                service->id, msg->details.serviceCA.ca ? "CA":"FTA");
            ServiceConditionalAccessSet(service, msg->details.serviceCA.ca);
            break;

        case CacheUpdate_Service_Type:
            service = msg->details.serviceType.service;
            LogModule(LOG_DEBUG, CACHE, "Updating Type for 0x%04x new Type %d\n",
                service->id, msg->details.serviceType.type);
            ServiceTypeSet(service, msg->details.serviceType.type);
            break;

        case CacheUpdate_Service_Provider:
            service = msg->details.serviceProvider.service;
            LogModule(LOG_DEBUG, CACHE, "Updating provider for 0x%04x new provider %s\n",
                service->id, service->provider);
            ServiceProviderSet(service, msg->details.serviceProvider.provider);
            free(msg->details.serviceProvider.provider);
            break;

        case CacheUpdate_Service_Default_Auth:
            service = msg->details.serviceDefaultAuthority.service;
            LogModule(LOG_DEBUG, CACHE, "Updating default authority for 0x%04x new authority %s\n",
                service->id, msg->details.serviceDefaultAuthority.defaultAuthority);
            ServiceDefaultAuthoritySet(service, msg->details.serviceDefaultAuthority.defaultAuthority);
            free(msg->details.serviceDefaultAuthority.defaultAuthority);
            break;

        case CacheUpdate_Service_Added:
            LogModule(LOG_DEBUG, CACHE, "Adding service 0x%04x\n", msg->details.serviceAdd.id);
            ServiceAdd(msg->details.serviceAdd.multiplexUID,
                       msg->details.serviceAdd.name, msg->details.serviceAdd.id,
                       msg->details.serviceAdd.source, FALSE, ServiceType_Unknown,
                       -1, 8192, 8192);
            free(msg->details.serviceAdd.name);
            break;

        case CacheUpdate_Service_Deleted:
            service = msg->details.serviceDelete.service;
            LogModule(LOG_DEBUG, CACHE, "Deleting service %s (0x%04x)\n", service->name, service->id);
            ServiceDelete(service);
            PIDListRemove(service);
            break;
    }
    DBaseTransactionCommit();

    if (mux)
    {
        ObjectRefDec(mux);
    }

    if (service)
    {
        ObjectRefDec(service);
    }

    ObjectRefDec(msg);
}
