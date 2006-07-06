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
#include <sys/time.h>

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
static void InformTSStructureChanged(TSFilter_t *state);

TSFilter_t* TSFilterCreate(DVBAdapter_t *adapter)
{
    TSFilter_t *result = calloc(1, sizeof(TSFilter_t));
    if (result)
    {
        result->adapter = adapter;
        pthread_mutex_init(&result->mutex, NULL);
        pthread_create(&result->thread, NULL, FilterTS, result);
    }
    return result;
}

void TSFilterDestroy(TSFilter_t* tsfilter)
{
    tsfilter->quit = TRUE;
    pthread_join(tsfilter->thread, NULL);
    pthread_mutex_destroy(&tsfilter->mutex);
}

void TSFilterEnable(TSFilter_t* tsfilter, bool enable)
{
    pthread_mutex_lock(&tsfilter->mutex);
    tsfilter->enabled = enable;
    pthread_mutex_unlock(&tsfilter->mutex);
}

void TSFilterZeroStats(TSFilter_t* tsfilter)
{
    int i;

    /* Clear all filter stats */
    tsfilter->totalpackets = 0;
    tsfilter->bitrate = 0;

    for (i = 0; i < MAX_FILTERS; i ++)
    {
        if (tsfilter->pidfilters[i].allocated)
        {
            tsfilter->pidfilters[i].filter.packetsfiltered  = 0;
            tsfilter->pidfilters[i].filter.packetsprocessed = 0;
            tsfilter->pidfilters[i].filter.packetsoutput    = 0;
        }
    }
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

    memset(&tsfilter->pidfilters[i].filter, 0, sizeof(PIDFilter_t));
    tsfilter->pidfilters[i].filter.tsfilter = tsfilter;
    tsfilter->pidfilters[i].allocated = 1;

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

PIDFilter_t *PIDFilterSetup(TSFilter_t *tsfilter,
                            PacketFilter filterpacket,  void *fparg,
                            PacketProcessor processpacket, void *pparg,
                            PacketOutput outputpacket,  void *oparg)
{
    PIDFilter_t *filter;

    filter = PIDFilterAllocate(tsfilter);
    if (filter)
    {
        filter->filterpacket = filterpacket;
        filter->fparg = fparg;

        filter->processpacket = processpacket;
        filter->pparg = pparg;

        filter->outputpacket = outputpacket;
        filter->oparg = oparg;
    }
    return filter;
}

int PIDFilterSimpleFilter(PIDFilter_t *pidfilter, void *arg, uint16_t pid, TSPacket_t *packet)
{
    PIDFilterSimpleFilter_t *filter = (PIDFilterSimpleFilter_t*)arg;
    int i;
    for (i = 0; i < filter->pidcount; i ++)
    {
        if ((pid == filter->pids[i]) ||
            (filter->pids[i] == 8192)) /* Special case match all PIDs */
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
	struct timeval now, last;
	int diff;
	int prevpackets = 0;

    TSFilter_t *state = (TSFilter_t*)arg;
    DVBAdapter_t *adapter = state->adapter;
    int count = 0;

	gettimeofday(&last, 0);

    while (!state->quit)
    {
        int p;
        //Read in packet
        count = DVBDVRRead(adapter, (char*)state->readbuffer, sizeof(state->readbuffer), 100);
        pthread_mutex_lock(&state->mutex);
        for (p = 0; (p < (count / TSPACKET_SIZE)) && state->enabled; p ++)
        {
            ProcessPacket(state, &state->readbuffer[p]);
            state->totalpackets ++;
            /* The structure of the transport stream has changed in a major way,
                (ie new services, services removed) so inform all of the filters
                that are interested.
              */
            if (state->tsstructurechanged)
            {
                InformTSStructureChanged(state);
                state->tsstructurechanged = FALSE;
            }
        }

		gettimeofday(&now, 0);
		diff =(now.tv_sec - last.tv_sec) * 1000 + (now.tv_usec - last.tv_usec) / 1000;
		if (diff > 1000)
		{
			// Work out bit rates
			state->bitrate = ((state->totalpackets - prevpackets) * (188 * 8));
			prevpackets = state->totalpackets;
			last = now;
		}
		pthread_mutex_unlock(&state->mutex);
    }
    return NULL;
}

static void ProcessPacket(TSFilter_t *state, TSPacket_t *packet)
{
    int i;
    uint16_t pid = TSPACKET_GETPID(*packet);

    for (i = 0; i < MAX_FILTERS; i ++)
    {
        if (state->pidfilters[i].allocated && state->pidfilters[i].filter.enabled)
        {
            PIDFilter_t *filter = &state->pidfilters[i].filter;
            if (filter->filterpacket && filter->filterpacket(filter, filter->fparg, pid, packet))
            {
                bool output = TRUE;
                TSPacket_t *outputPacket = packet;

                filter->packetsfiltered ++;

                if (filter->processpacket)
                {
                    outputPacket = filter->processpacket(filter, filter->pparg, packet);
                    output = (outputPacket) ? TRUE:FALSE;
                    filter->packetsprocessed ++;
                }

                if (output && filter->outputpacket)
                {
                    filter->outputpacket(filter, filter->oparg, outputPacket);
                    filter->packetsoutput ++;
                }
            }
        }
    }
}

static void InformTSStructureChanged(TSFilter_t *state)
{
    int i;

    for (i = 0; i < MAX_FILTERS; i ++)
    {
        if (state->pidfilters[i].allocated &&
            state->pidfilters[i].filter.enabled &&
            state->pidfilters[i].filter.tsstructurechanged)
        {
            PIDFilter_t *filter = &state->pidfilters[i].filter;
            filter->tsstructurechanged(filter, filter->tscarg);
        }
    }
}
