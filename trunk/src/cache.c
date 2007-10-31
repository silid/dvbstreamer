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
    
#define _GNU_SOURCE
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
/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/

#define SERVICES_MAX (256)

/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/

enum CacheFlags
{
    CacheFlag_Clean         = 0x0000,
    CacheFlag_Dirty_PMTPID  = 0x0001,
    CacheFlag_Dirty_PIDs    = 0x0002, /* Also means PMT Version and PCR PID needs to be updated */
    CacheFlag_Dirty_Name    = 0x0004,
    CacheFlag_Dirty_Source  = 0x0008,    
    CacheFlag_Dirty_CA      = 0x0010,
    CacheFlag_Dirty_Type    = 0x0020,
    CacheFlag_Dirty_Provider= 0x0040,
    CacheFlag_Dirty_DefAuth = 0x0060,
    CacheFlag_Dirty_Added   = 0x8000,
};

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void CacheServicesFree(void);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static char CACHE[] = "Cache";
static int cachedServicesMultiplexDirty = 0;
static Multiplex_t *cachedServicesMultiplex = NULL;
static int cachedServicesCount = 0;
static int cachedDeletedServicesCount = 0;

static pthread_mutex_t cacheUpdateMutex;
static enum CacheFlags cacheFlags[SERVICES_MAX];
static Service_t*      cachedServices[SERVICES_MAX];
static Service_t*      cachedDeletedServices[SERVICES_MAX];
static PIDList_t*      cachedPIDs[SERVICES_MAX];

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/

int CacheInit()
{
    pthread_mutex_init(&cacheUpdateMutex, NULL);
    return 0;
}

void CacheDeInit()
{
    CacheWriteback();
    CacheServicesFree();
    pthread_mutex_destroy(&cacheUpdateMutex);
}

int CacheLoad(Multiplex_t *multiplex)
{
    int result = 1;
    int count = ServiceForMultiplexCount(multiplex->uid);

    pthread_mutex_lock(&cacheUpdateMutex);
    LogModule(LOG_DEBUG, CACHE, "Freeing services\n");

    /* Free the services and PIDs from the previous multiplex */
    CacheServicesFree();

    LogModule(LOG_DEBUG, CACHE, "Loading %d services for %d\n", count, multiplex->uid);
    if (count > 0)
    {
        int i;
        ServiceEnumerator_t enumerator;

        enumerator = ServiceEnumeratorForMultiplex(multiplex);
        for (i=0; i < count; i++)
        {
            cachedServices[i] = ServiceGetNext(enumerator);
            LogModule(LOG_DEBUG,CACHE, "Loaded 0x%04x %s\n", cachedServices[i]->id, cachedServices[i]->name);
            cachedPIDs[i] = PIDListGet(cachedServices[i]);
            cacheFlags[i] = CacheFlag_Clean;
        }
        ServiceEnumeratorDestroy(enumerator);
    }
    
    cachedServicesCount = count;

    MultiplexRefInc(multiplex);
    cachedServicesMultiplex = multiplex;
    cachedServicesMultiplexDirty = 0;
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
    pthread_mutex_lock(&cacheUpdateMutex);

    if (cachedServicesMultiplex && MultiplexAreEqual(multiplex, cachedServicesMultiplex))
    {
        cachedServicesMultiplex->patVersion = patversion;
        cachedServicesMultiplex->tsId = tsid;
        cachedServicesMultiplexDirty = 1;
    }

    pthread_mutex_unlock(&cacheUpdateMutex);
}

void CacheUpdateNetworkId(Multiplex_t *multiplex, int netid)
{
    pthread_mutex_lock(&cacheUpdateMutex);

    if (cachedServicesMultiplex && MultiplexAreEqual(multiplex, cachedServicesMultiplex))
    {
        cachedServicesMultiplex->networkId = netid;
        cachedServicesMultiplexDirty = 1;
    }

    pthread_mutex_unlock(&cacheUpdateMutex);
}

void CacheUpdateService(Service_t *service, int pmtpid)
{
    int i;
    pthread_mutex_lock(&cacheUpdateMutex);

    for (i = 0; i < cachedServicesCount; i ++)
    {
        if ((cachedServices[i]) && ServiceAreEqual(service, cachedServices[i]))
        {
            cachedServices[i]->pmtPid = pmtpid;
            cacheFlags[i] |= CacheFlag_Dirty_PMTPID;
            break;
        }
    }

    pthread_mutex_unlock(&cacheUpdateMutex);
}

void CacheUpdateServiceName(Service_t *service, char *name)
{
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
            cacheFlags[i] |= CacheFlag_Dirty_Name;
            break;
        }
    }

    pthread_mutex_unlock(&cacheUpdateMutex);
}

void CacheUpdateServiceProvider(Service_t *service, char *provider)
{
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
            cacheFlags[i] |= CacheFlag_Dirty_Provider;
            break;
        }
    }

    pthread_mutex_unlock(&cacheUpdateMutex);
}

void CacheUpdateServiceDefaultAuthority(Service_t *service, char *defaultAuthority)
{
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
            cacheFlags[i] |= CacheFlag_Dirty_DefAuth;
            break;
        }
    }

    pthread_mutex_unlock(&cacheUpdateMutex);
}

void CacheUpdateServiceSource(Service_t *service, uint16_t source)
{
    int i;
    pthread_mutex_lock(&cacheUpdateMutex);

    for (i = 0; i < cachedServicesCount; i ++)
    {
        if ((cachedServices[i]) && ServiceAreEqual(service, cachedServices[i]))
        {
            cachedServices[i]->source = source;
            cacheFlags[i] |= CacheFlag_Dirty_Source;
            break;
        }
    }

    pthread_mutex_unlock(&cacheUpdateMutex);
}

void CacheUpdateServiceConditionalAccess(Service_t *service, bool ca)
{
    int i;
    pthread_mutex_lock(&cacheUpdateMutex);

    for (i = 0; i < cachedServicesCount; i ++)
    {
        if ((cachedServices[i]) && ServiceAreEqual(service, cachedServices[i]))
        {
            cachedServices[i]->conditionalAccess = ca;
            cacheFlags[i] |= CacheFlag_Dirty_CA;
            break;
        }
    }

    pthread_mutex_unlock(&cacheUpdateMutex);
}

void CacheUpdateServiceType(Service_t *service, ServiceType type)
{
    int i;
    pthread_mutex_lock(&cacheUpdateMutex);

    for (i = 0; i < cachedServicesCount; i ++)
    {
        if ((cachedServices[i]) && ServiceAreEqual(service, cachedServices[i]))
        {
            cachedServices[i]->type = type;
            cacheFlags[i] |= CacheFlag_Dirty_Type;
            break;
        }
    }

    pthread_mutex_unlock(&cacheUpdateMutex);
}

void CacheUpdatePIDs(Service_t *service, int pcrpid, PIDList_t *pids, int pmtversion)
{
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
            cacheFlags[i] |= CacheFlag_Dirty_PIDs;
            break;
        }
    }

    pthread_mutex_unlock(&cacheUpdateMutex);
}

Service_t *CacheServiceAdd(int id)
{
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
        asprintf(&result->name, "%04x", id);
        result->multiplexUID = cachedServicesMultiplex->uid;
        
        pthread_mutex_lock(&cacheUpdateMutex);

        LogModule(LOG_DEBUG, CACHE, "Added service %04x at %d\n", result->id, cachedServicesCount);
        ServiceRefInc(result);
        cachedServices[cachedServicesCount] = result;
        cachedPIDs[cachedServicesCount] = NULL;
        cacheFlags[cachedServicesCount]  = CacheFlag_Dirty_Added;
        cachedServicesCount ++;

        pthread_mutex_unlock(&cacheUpdateMutex);
    }
    return result;
}

void CacheServiceDelete(Service_t *service)
{
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
        
        /* Add the deleted service to the list of deleted services for removal
            when we writeback the cache.
         */
        cachedDeletedServices[cachedDeletedServicesCount] = service;
        cachedDeletedServicesCount ++;
    }

    pthread_mutex_unlock(&cacheUpdateMutex);
}

void CacheWriteback()
{
    int i;

    pthread_mutex_lock(&cacheUpdateMutex);
    LogModule(LOG_DEBUG, CACHE, "Writing changes to cache back to database\n");

    DBaseTransactionBegin();

    /* Delete deleted services from the database along with their PIDs
       NOTE: Delete these first encase we have seen them again after
       they where initial deleted.
    */
    for (i = 0; i < cachedDeletedServicesCount; i ++)
    {
        LogModule(LOG_DEBUG, CACHE, "Deleting service %s (0x%04x)\n", cachedDeletedServices[i]->name, cachedDeletedServices[i]->id);
        ServiceDelete(cachedDeletedServices[i]);
        PIDListRemove(cachedDeletedServices[i]);
        ServiceRefDec(cachedDeletedServices[i]);
    }
    cachedDeletedServicesCount = 0;

    if (cachedServicesMultiplexDirty)
    {
        int rc;
        LogModule(LOG_DEBUG, CACHE, "Updating Multiplex PAT version\n");
        rc =MultiplexPATVersionSet(cachedServicesMultiplex, cachedServicesMultiplex->patVersion);
        if (rc)
        {
            LogModule(LOG_ERROR, CACHE, "Failed to update Multiplex PAT version (0x%x)\n", rc);
        }
        rc = MultiplexTSIdSet(cachedServicesMultiplex, cachedServicesMultiplex->tsId);
        if (rc)
        {
            LogModule(LOG_ERROR, CACHE, "Failed to update Multiplex TS ID (0x%x)\n", rc);
        }
        rc = MultiplexNetworkIdSet(cachedServicesMultiplex, cachedServicesMultiplex->networkId);
        if (rc)
        {
            LogModule(LOG_ERROR, CACHE, "Failed to update Multiplex Original Network ID (0x%x)\n", rc);
        }
        cachedServicesMultiplexDirty = 0;
    }

    for (i = 0; i < cachedServicesCount; i ++)
    {
        if (cacheFlags[i] & CacheFlag_Dirty_Added)
        {
            LogModule(LOG_DEBUG, CACHE, "Adding service %s (0x%04x)\n", cachedServices[i]->name, cachedServices[i]->id);
            ServiceAdd(cachedServices[i]->multiplexUID, cachedServices[i]->name, cachedServices[i]->id,
                cachedServices[i]->source, cachedServices[i]->conditionalAccess,  cachedServices[i]->type,
                cachedServices[i]->pmtVersion, cachedServices[i]->pmtPid, 
                cachedServices[i]->pcrPid);
            cacheFlags[i] &= ~(CacheFlag_Dirty_Name | CacheFlag_Dirty_PMTPID | 
                               CacheFlag_Dirty_Source | CacheFlag_Dirty_CA | CacheFlag_Dirty_Type);
        }

        if (cacheFlags[i] & CacheFlag_Dirty_PMTPID)
        {
            LogModule(LOG_DEBUG, CACHE, "Updating PMT PID for %s\n", cachedServices[i]->name);
            ServicePMTPIDSet(cachedServices[i], cachedServices[i]->pmtPid);
        }
        if (cacheFlags[i] & CacheFlag_Dirty_PIDs)
        {
            LogModule(LOG_DEBUG, CACHE, "Updating PIDs for %s\n", cachedServices[i]->name);
            PIDListRemove(cachedServices[i]);
            PIDListSet(cachedServices[i], cachedPIDs[i]);
            ServicePMTVersionSet(cachedServices[i], cachedServices[i]->pmtVersion);
            ServicePCRPIDSet(cachedServices[i], cachedServices[i]->pcrPid);
        }

        if (cacheFlags[i] & CacheFlag_Dirty_Name)
        {
            LogModule(LOG_DEBUG, CACHE, "Updating name for 0x%04x new name %s\n", cachedServices[i]->id, cachedServices[i]->name);
            ServiceNameSet(cachedServices[i], cachedServices[i]->name);
        }
        if (cacheFlags[i] & CacheFlag_Dirty_Source)
        {
            LogModule(LOG_DEBUG, CACHE, "Updating source for 0x%04x new source %x\n", cachedServices[i]->id, cachedServices[i]->source);
            ServiceSourceSet(cachedServices[i], cachedServices[i]->source);
        }
        if (cacheFlags[i] & CacheFlag_Dirty_CA)
        {
            LogModule(LOG_DEBUG, CACHE, "Updating CA state for 0x%04x new CA state %s\n", cachedServices[i]->id, cachedServices[i]->conditionalAccess ? "CA":"FTA");
            ServiceConditionalAccessSet(cachedServices[i], cachedServices[i]->conditionalAccess);
        }
        if (cacheFlags[i] & CacheFlag_Dirty_Type)
        {
            LogModule(LOG_DEBUG, CACHE, "Updating Type for 0x%04x new Type %d\n", cachedServices[i]->id, cachedServices[i]->type);
            ServiceTypeSet(cachedServices[i], cachedServices[i]->type);
        }        
        if (cacheFlags[i] & CacheFlag_Dirty_Provider)
        {
            LogModule(LOG_DEBUG, CACHE, "Updating provider for 0x%04x new provider %s\n", cachedServices[i]->id, cachedServices[i]->provider);
            ServiceProviderSet(cachedServices[i], cachedServices[i]->provider);
        }
        if (cacheFlags[i] & CacheFlag_Dirty_DefAuth)
        {
            LogModule(LOG_DEBUG, CACHE, "Updating default authority for 0x%04x new authority %s\n", cachedServices[i]->id, cachedServices[i]->defaultAuthority);
            ServiceDefaultAuthoritySet(cachedServices[i], cachedServices[i]->defaultAuthority);
        }
        cacheFlags[i] = 0;
    }

    DBaseTransactionCommit();
    pthread_mutex_unlock(&cacheUpdateMutex);
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

