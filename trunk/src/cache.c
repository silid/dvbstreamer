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
/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/

#define SERVICES_MAX (256)

/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/

enum CacheFlags
{
    CacheFlag_Clean         = 0x00,
    CacheFlag_Dirty_PMTPID  = 0x01,
    CacheFlag_Dirty_PIDs    = 0x02, /* Also means PMT Version and PCR PID needs to be updated */
    CacheFlag_Dirty_Name    = 0x04,
    CacheFlag_Dirty_Added   = 0x10,
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

Service_t *CacheServiceFindName(char *name, Multiplex_t **multiplex)
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
            *multiplex = cachedServicesMultiplex;
            MultiplexRefInc(*multiplex);
            LogModule(LOG_DEBUGV, CACHE, "Found in cached services!\n");
            break;
        }
    }

    if (result == NULL)
    {
        LogModule(LOG_DEBUGV, CACHE, "Not found in cached services\n");
        result = ServiceFindName(name);
        if (result)
        {
            *multiplex = MultiplexFind(result->multiplexUID);
        }
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
            strncpy(cachedServices[i]->name, name, SERVICE_MAX_NAME_LEN);
            cacheFlags[i] |= CacheFlag_Dirty_Name;
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
        result->pmtVersion = -1;
        result->pmtPid = 8192;
        sprintf(result->name, "%04x", id);
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

    sqlite3_exec(DBaseInstance, "BEGIN TRANSACTION;", NULL, NULL, NULL);

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
                cachedServices[i]->pmtVersion, cachedServices[i]->pmtPid, cachedServices[i]->pcrPid);
            cacheFlags[i] &= ~CacheFlag_Dirty_Name;
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
        cacheFlags[i] = 0;
    }



    sqlite3_exec(DBaseInstance, "COMMIT TRANSACTION;", NULL, NULL, NULL);
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

