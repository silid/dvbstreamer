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

ts.c

Transport stream processing and filter management.

*/
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <dvbpsi/dvbpsi.h>
#include <dvbpsi/descriptor.h>
#include <dvbpsi/psi.h>
#include <dvbpsi/pat.h>
#include <dvbpsi/pmt.h>
#include "multiplexes.h"
#include "services.h"
#include "ts.h"

static void *FilterTS(void *arg);
static void ProcessPacket(TSFilter_t *state, TSPacket_t *packet);

TSFilter_t* TSFilterCreate(DVBAdapter_t *adapter)
{
	TSFilter_t *result = calloc(1, sizeof(TSFilter_t));
	if (result)
	{
		result->adapter = adapter;
		//pthread_mutex_init(&result->pidmutex, NULL);
		pthread_create(&result->thread, NULL, FilterTS, result);
	}
	return result;
}

void TSFilterDestroy(TSFilter_t* tsfilter)
{
	tsfilter->quit = 1;
	pthread_join(tsfilter->thread, NULL);
	//pthread_mutex_destroy(tsfilter->pidmutex);
}

void TSFilterEnable(TSFilter_t* tsfilter, int enable)
{
	tsfilter->enable = enable;
}

PIDFilter_t* PIDFilterAllocate(TSFilter_t* tsfilter)
{
	int i = 0;
	for (i = 0; i < MAX_FILTERS; i ++)
	{
		if (tsfilter->pidfilters[i].allocated == 0)
		{
			break;
		}
	}
	if (i == MAX_FILTERS)
	{
		return NULL;
	}
	tsfilter->pidfilters[i].allocated = 1;
	tsfilter->pidfilters[i].filter.tsfilter = tsfilter;
	tsfilter->pidfilters[i].filter.enabled = 0;
	memset(&tsfilter->pidfilters[i].filter, 0, sizeof(PIDFilter_t));
	return &tsfilter->pidfilters[i].filter;
}

void PIDFilterFree(PIDFilter_t * pidfilter)
{
	int i;
	for (i = 0; i < MAX_FILTERS; i ++)
	{
		if (&pidfilter->tsfilter->pidfilters[i].filter == pidfilter)
		{
			pidfilter->tsfilter->pidfilters[i].allocated = 0;
		}
	}
}

int PIDFilterSimpleFilter(void *arg, TSPacket_t *packet)
{
	PIDFilterSimpleFilter_t *filter = (PIDFilterSimpleFilter_t*)arg;
	int i;
	unsigned short pid = TSPACKET_GETPID(*packet);
	for (i = 0; i < filter->pidcount; i ++)
	{
		if (pid == filter->pids[i])
		{
			return 1;
		}
	}
	return 0;
}
/******************************************************************************
* Internal Functions                                                          *
******************************************************************************/
static void *FilterTS(void *arg)
{
    
    TSFilter_t *state = (TSFilter_t*)arg;
    DVBAdapter_t *adapter = state->adapter;
	int count = 0;

    printf("FilterTS(%p): Started\n", state);
    while (!state->quit)
    {
        int p;
        //Read in packet
        count = DVBDVRRead(adapter, (char*)state->readBuffer, sizeof(state->readBuffer), 100);
		for (p = 0; (p < (count / TSPACKET_SIZE)) && state->enable; p ++)
		{
				ProcessPacket(state, &state->readBuffer[p]);
		}
    }
	printf("FilterTS(%p): Finished\n", state);
	return NULL;
}

static void ProcessPacket(TSFilter_t *state, TSPacket_t *packet)
{
	int i;
	for (i = 0; i < MAX_FILTERS; i ++)
	{
		if (state->pidfilters[i].allocated && state->pidfilters[i].filter.enabled)
		{
			PIDFilter_t *filter = &state->pidfilters[i].filter;
			if (filter->filterpacket && filter->filterpacket(filter->fparg, packet))
			{
				int output = 1;
				filter->packetsprocessed ++;
				if (filter->processpacket)
				{
					output = filter->processpacket(filter->pparg, packet);
				}
				if (output && filter->outputpacket)
				{
					filter->outputpacket(filter->oparg, packet);
				}
			}
		}
	}
}
