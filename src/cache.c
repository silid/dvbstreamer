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
#include <stdlib.h>
#include <string.h>
#include "ts.h"
#include "multiplexes.h"
#include "services.h"

#define SERVICES_MAX (256)

enum CacheFlags
{
	CacheFlag_Clean        = 0x00,
	CacheFlag_Dirty_PMTPID = 0x01,
	CacheFlag_Dirty_PIDs   = 0x02, /* Also means PMT Version needs to be updated */
};
static int cachedServicesMultiplexDirty = 0;
static Multiplex_t *cachedServicesMultiplex = NULL;
static int cachedServicesCount = 0;

static enum CacheFlags cacheFlags[SERVICES_MAX];
static Service_t*      cachedServices[SERVICES_MAX];
static int             cachedPIDsCount[SERVICES_MAX];
static PID_t*          cachedPIDs[SERVICES_MAX];

static void CacheServicesFree();
static void CachePIDsFree();

int CacheInit()
{
	// Do nothing for the moment;
	return 0;
}

void CacheDeInit()
{
	CacheServicesFree();
	CachePIDsFree();
}

int CacheLoad(Multiplex_t *multiplex)
{
	int count = ServiceForMultiplexCount(multiplex->freq);
	printlog(1,"Loading %d services for %d\n", count, multiplex->freq);
	if (count > 0)
	{
		int i;
		ServiceEnumerator_t *enumerator;
		
		CacheServicesFree();

		enumerator = ServiceEnumeratorForMultiplex(multiplex->freq);
		for (i=0; i < count; i++)
		{
			cachedServices[i] = ServiceGetNext(enumerator);
			printlog(1,"Loaded 0x%04x %s\n", cachedServices[i]->id, cachedServices[i]->name);
			CachePIDsLoad(cachedServices[i], i);
			cacheFlags[i] = CacheFlag_Clean;
		}
		ServiceEnumeratorDestroy(enumerator);
		cachedServicesCount = count;
		cachedServicesMultiplex = multiplex;
		cachedServicesMultiplexDirty = 0;
		return 0;
	}
	return 1;
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
		printlog(1,"Checking cached services..");
		for (i = 0; i < cachedServicesCount; i ++)
		{
			if (strcmp(cachedServices[i]->name, name) == 0)
			{
				result = cachedServices[i];
				*multiplex = cachedServicesMultiplex;
				printlog(1,"Found in cached services!\n");
				break;
			}
		}
	}
	if (result == NULL)
	{
		if (cachedServices)
		{
			printlog(1,"Not found in cached services\n");
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

int CachePIDsLoad(Service_t *service, int index)
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
	if (cachedServicesMultiplex && MultiplexAreEqual(multiplex, cachedServicesMultiplex))
	{
		cachedServicesMultiplex->patversion = patversion;
		cachedServicesMultiplex->tsid = tsid;
		cachedServicesMultiplexDirty = 1;
	}
}
void CacheUpdateService(Service_t *service, int pmtpid)
{
	int i;
	for (i = 0; i < cachedServicesCount; i ++)
	{
		if ((cachedServices[i]) && ServiceAreEqual(service, cachedServices[i]))
		{
			cachedServices[i]->pmtpid = pmtpid;
			cacheFlags[i] |= CacheFlag_Dirty_PMTPID;
			break;
		}
	}
}

void CacheUpdatePIDs(Service_t *service, PID_t *pids, int count, int pmtversion)
{
	int i;
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
}

void CacheWriteback()
{
	int i;

	if (cachedServicesMultiplexDirty)
	{
		MultiplexPATVersionSet(cachedServicesMultiplex, cachedServicesMultiplex->patversion);
		MultiplexTSIdSet(cachedServicesMultiplex, cachedServicesMultiplex->tsid);
		cachedServicesMultiplex = 0;
	}
	
	for (i = 0; i < cachedServicesCount; i ++)
	{
		if (cacheFlags[i] & CacheFlag_Dirty_PMTPID)
		{
			ServicePMTPIDSet(cachedServices[i], cachedServices[i]->pmtpid);
			cacheFlags[i] ^= CacheFlag_Dirty_PMTPID;
		}
		if (cacheFlags[i] & CacheFlag_Dirty_PIDs)
		{
			int p;
			PID_t *pids = cachedPIDs[i];			
			ServicePIDRemove(cachedServices[i]);
			for ( p = 0; p < cachedPIDsCount[i]; p ++)
			{
				ServicePIDAdd(cachedServices[i], pids[p].pid, pids[p].type, pids[p].subtype, cachedServices[i]->pmtversion);
			}
			ServicePMTVersionSet(cachedServices[i], cachedServices[i]->pmtversion);
			cacheFlags[i] ^= CacheFlag_Dirty_PIDs;
		}
	}
}

static void CacheServicesFree()
{
	if (cachedServices)
	{
		int i;
		for (i = 0; i < cachedServicesCount; i ++)
		{
			ServiceFree(cachedServices[i]);
		}
		cachedServicesCount = 0;
		cachedServicesMultiplex = NULL;
	}
}

static void CachePIDsFree()
{
	if (cachedPIDs)
	{
		int i;
		for (i = 0; i < cachedServicesCount; i ++)
		{
			free(cachedPIDs[i]);
			cachedPIDsCount[i] = 0;
		}
	}
}
