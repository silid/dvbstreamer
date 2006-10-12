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
#include "logging.h"
#include "cache.h"
#include "dbase.h"

#define SERVICES_MAX (256)

enum CacheFlags
{
    CacheFlag_Clean         = 0x00,
    CacheFlag_Dirty_PMTPID  = 0x01,
    CacheFlag_Dirty_PIDs    = 0x02, /* Also means PMT Version needs to be updated */
    CacheFlag_Dirty_Name    = 0x04,
    CacheFlag_Dirty_Added   = 0x10,
};

static int cachedServicesMultiplexDirty = 0;
static Multiplex_t *cachedServicesMultiplex = NULL;
static int cachedServicesCount = 0;
static int cachedDeletedServicesCount = 0;

static pthread_mutex_t cacheUpdateMutex;
static enum CacheFlags cacheFlags[SERVICES_MAX];
static Service_t*      cachedServices[SERVICES_MAX];
static Service_t*      cachedDeletedServices[SERVICES_MAX];
static int             cachedPIDsCount[SERVICES_MAX];
static PID_t*          cachedPIDs[SERVICES_MAX];

static void CacheServicesFree();
static void CachePIDsFree();
static int CachePIDsLoad(Service_t *service, int index);

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
    int count = ServiceForMultiplexCount(multiplex->freq);

    pthread_mutex_lock(&cacheUpdateMutex);

    /* Free the services and PIDs from the previous multiplex */
    CacheServicesFree();

    printlog(LOG_DEBUG,"Loading %d services for %d\n", count, multiplex->freq);
    if (count > 0)
    {
        int i;
        ServiceEnumerator_t *enumerator;

        enumerator = ServiceEnumeratorForMultiplex(multiplex->freq);
        for (i=0; i < count; i++)
        {
            cachedServices[i] = ServiceGetNext(enumerator);
            printlog(LOG_DEBUG,"Loaded 0x%04x %s\n", cachedServices[i]->id, cachedServices[i]->name);
            CachePIDsLoad(cachedServices[i], i);
            cacheFlags[i] = CacheFlag_Clean;
        }
        ServiceEnumeratorDestroy(enumerator);
        cachedServicesCount = count;
        cachedServicesMultiplex = multiplex;
        cachedServicesMultiplexDirty = 0;
        result = 0;
    }

    pthread_mutex_unlock(&cacheUpdateMutex);

    return result;
}

Service_t *CacheServiceFindId(int id)
{
    Service_t *result = NULL;
    int i;
    if (cachedServices)
    {
        for (i = 0; i < cachedServicesCount; i ++)
        {
            if (cachedServices[i]->id == id)
            {
                result = cachedServices[i];
                break;
            }
        }
    }
    return result;
}

Service_t *CacheServiceFindName(char *name, Multiplex_t **multiplex)
{
    Service_t *result = NULL;
    int i;
    if (cachedServices)
    {
        printlog(LOG_DEBUGV,"Checking cached services for \"%s\"\n", name);
        for (i = 0; i < cachedServicesCount; i ++)
        {
            printlog(LOG_DEBUGV, "cachedServices[%d]->name = %s\n", i, cachedServices[i]->name);
            if (strcmp(cachedServices[i]->name, name) == 0)
            {
                result = cachedServices[i];
                *multiplex = cachedServicesMultiplex;
                printlog(LOG_DEBUGV,"Found in cached services!\n");
                break;
            }
        }
    }
    if (result == NULL)
    {
        if (cachedServices)
        {
            printlog(LOG_DEBUGV,"Not found in cached services\n");
        }
        result = ServiceFindName(name);
        if (result)
        {
            *multiplex = MultiplexFind(result->multiplexfreq);
        }
    }
    return result;
}

Service_t **CacheServicesGet(int *count)
{
    *count = cachedServicesCount;
    return cachedServices;
}

static int CachePIDsLoad(Service_t *service, int index)
{
    int count = ServicePIDCount(service);
    if (count > 0)
    {
        cachedPIDs[index] = calloc(count, sizeof(PID_t));
        if (!cachedPIDs[index])
        {
            return 1;
        }
        ServicePIDGet(service, cachedPIDs[index], &count);
        cachedPIDsCount[index] = count;
        return 0;
    }
    return 1;
}

PID_t *CachePIDsGet(Service_t *service, int *count)
{
    PID_t *result = NULL;
    int i;
    *count = 0;
    for (i = 0; i < cachedServicesCount; i ++)
    {
        if ((cachedServices[i]) && ServiceAreEqual(service, cachedServices[i]))
        {
            result = cachedPIDs[i];
            *count = cachedPIDsCount[i];
            break;
        }
    }
    return result;
}

void CacheUpdateMultiplex(Multiplex_t *multiplex, int patversion, int tsid)
{
    pthread_mutex_lock(&cacheUpdateMutex);

    if (cachedServicesMultiplex && MultiplexAreEqual(multiplex, cachedServicesMultiplex))
    {
        cachedServicesMultiplex->patversion = patversion;
        cachedServicesMultiplex->tsid = tsid;
        cachedServicesMultiplexDirty = 1;
    }

    pthread_mutex_unlock(&cacheUpdateMutex);
}

void CacheUpdateNetworkId(Multiplex_t *multiplex, int netid)
{
    pthread_mutex_lock(&cacheUpdateMutex);

    if (cachedServicesMultiplex && MultiplexAreEqual(multiplex, cachedServicesMultiplex))
    {
        cachedServicesMultiplex->netid = netid;
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
            cachedServices[i]->pmtpid = pmtpid;
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
            free(cachedServices[i]->name);
            cachedServices[i]->name = strdup(name);
            cacheFlags[i] |= CacheFlag_Dirty_Name;
            break;
        }
    }

    pthread_mutex_unlock(&cacheUpdateMutex);
}

void CacheUpdatePIDs(Service_t *service, PID_t *pids, int count, int pmtversion)
{
    int i;
    pthread_mutex_lock(&cacheUpdateMutex);

    for (i = 0; i < cachedServicesCount; i ++)
    {
        if ((cachedServices[i]) && ServiceAreEqual(service, cachedServices[i]))
        {
            if (cachedPIDs[i])
            {
                free(cachedPIDs[i]);
            }
            cachedPIDs[i] = pids;
            cachedPIDsCount[i] = count;
            cachedServices[i]->pmtversion = pmtversion;
            cacheFlags[i] |= CacheFlag_Dirty_PIDs;
            break;
        }
    }

    pthread_mutex_unlock(&cacheUpdateMutex);
}

Service_t *CacheServiceAdd(int id)
{
    Service_t *result = calloc(1, sizeof(Service_t));
    if (result)
    {
        result->id = id;
        result->pmtversion = -1;
        result->pmtpid = 8192;
        result->name = malloc(5);
        sprintf(result->name, "%04x", id);
        result->multiplexfreq = cachedServicesMultiplex->freq;

        pthread_mutex_lock(&cacheUpdateMutex);

        cachedServices[cachedServicesCount] = result;
        cachedPIDs[cachedServicesCount] = NULL;
        cachedPIDsCount[cachedServicesCount] = 0;
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
        /* Get rid of the pids as we don't need them any more! */
        free(cachedPIDs[deletedIndex]);
        /* Remove the deleted service from the list */
        for (i = deletedIndex; i < cachedServicesCount; i ++)
        {
            cachedPIDs[i] = cachedPIDs[i + 1];
            cachedServices[i] = cachedServices[i +1];
            cacheFlags[i] = cacheFlags [i + 1];
        }
        cachedServicesCount --;
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
    printlog(LOG_DEBUG, "Writing changes to cache back to database\n");

    sqlite3_exec(DBaseInstance, "BEGIN TRANSACTION;", NULL, NULL, NULL);

    if (cachedServicesMultiplexDirty)
    {
        int rc;
        printlog(LOG_DEBUG, "Updating Multiplex PAT version\n");
        rc =MultiplexPATVersionSet(cachedServicesMultiplex, cachedServicesMultiplex->patversion);
        if (rc)
        {
            printlog(LOG_ERROR, "Failed to update Multiplex PAT version (0x%x)", rc);
        }
        rc = MultiplexTSIdSet(cachedServicesMultiplex, cachedServicesMultiplex->tsid);
        if (rc)
        {
            printlog(LOG_ERROR, "Failed to update Multiplex TS ID (0x%x)", rc);
        }
        rc = MultiplexNetworkIdSet(cachedServicesMultiplex, cachedServicesMultiplex->netid);
        if (rc)
        {
            printlog(LOG_ERROR, "Failed to update Multiplex Original Network ID (0x%x)", rc);
        }
        cachedServicesMultiplexDirty = 0;
    }

    for (i = 0; i < cachedServicesCount; i ++)
    {
        if (cacheFlags[i] & CacheFlag_Dirty_Added)
        {
            printlog(LOG_DEBUG, "Adding service %s (0x%04x)\n", cachedServices[i]->name, cachedServices[i]->id);
            ServiceAdd(cachedServices[i]->multiplexfreq, cachedServices[i]->name, cachedServices[i]->id, cachedServices[i]->pmtversion, cachedServices[i]->pmtpid);
            cacheFlags[i] = 0;
            continue;
        }

        if (cacheFlags[i] & CacheFlag_Dirty_PMTPID)
        {
            printlog(LOG_DEBUG, "Updating PMT PID for %s\n", cachedServices[i]->name);
            ServicePMTPIDSet(cachedServices[i], cachedServices[i]->pmtpid);
        }
        if (cacheFlags[i] & CacheFlag_Dirty_PIDs)
        {
            int p;
            PID_t *pids = cachedPIDs[i];
            printlog(LOG_DEBUG, "Updating PIDs for %s\n", cachedServices[i]->name);
            ServicePIDRemove(cachedServices[i]);
            for ( p = 0; p < cachedPIDsCount[i]; p ++)
            {
                ServicePIDAdd(cachedServices[i], pids[p].pid, pids[p].type, pids[p].subtype, cachedServices[i]->pmtversion);
            }
            ServicePMTVersionSet(cachedServices[i], cachedServices[i]->pmtversion);
        }

        if (cacheFlags[i] & CacheFlag_Dirty_Name)
        {
            printlog(LOG_DEBUG, "Updating name for 0x%04x new name %s\n", cachedServices[i]->id, cachedServices[i]->name);
            ServiceNameSet(cachedServices[i], cachedServices[i]->name);
        }
        cacheFlags[i] = 0;
    }

    /* Delete deleted services from the database along with their PIDs*/
    for (i = 0; i < cachedDeletedServicesCount; i ++)
    {
        printlog(LOG_DEBUG, "Deleting service %s (0x%04x)\n", cachedDeletedServices[i]->name, cachedDeletedServices[i]->id);
        ServiceDelete(cachedDeletedServices[i]);
        ServicePIDRemove(cachedDeletedServices[i]);
        ServiceFree(cachedDeletedServices[i]);
    }
    cachedDeletedServicesCount = 0;

    sqlite3_exec(DBaseInstance, "COMMIT TRANSACTION;", NULL, NULL, NULL);
    pthread_mutex_unlock(&cacheUpdateMutex);
}

static void CacheServicesFree()
{
    if (cachedServices)
    {
        int i;
        for (i = 0; i < cachedServicesCount; i ++)
        {
            if (cachedServices[i])
            {
                ServiceFree(cachedServices[i]);
            }
            if (cachedPIDs[i])
            {
                free(cachedPIDs[i]);
            }
            cachedPIDsCount[i] = 0;
        }
        cachedServicesCount = 0;
        cachedServicesMultiplex = NULL;
    }
}

