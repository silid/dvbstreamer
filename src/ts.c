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
#include "multiplexes.h"
#include "services.h"
#define __USE_UNIX98
#include "ts.h"
#include "logging.h"

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void *FilterTS(void *arg);
static void ProcessPacket(TSFilter_t *state, TSPacket_t *packet);
static void InformTSStructureChanged(TSFilter_t *state);
static void InformMultiplexChanged(TSFilter_t *state);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/

char PSISIPIDFilterType[] = "PSI/SI";

/*******************************************************************************
* Transport Stream Filter Functions                                            *
*******************************************************************************/
TSFilter_t* TSFilterCreate(DVBAdapter_t *adapter)
{
    TSFilter_t *result;
    ObjectRegisterType(TSFilter_t);
    result = ObjectCreateType(TSFilter_t);
    if (result)
    {
        pthread_mutexattr_t mutexAttr;

        result->adapter = adapter;
        result->pidFilters = ListCreate();

        pthread_mutexattr_init(&mutexAttr);
        pthread_mutexattr_settype(&mutexAttr, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&result->mutex, &mutexAttr);
        pthread_mutexattr_destroy(&mutexAttr);
        
        pthread_create(&result->thread, NULL, FilterTS, result);
    }
    return result;
}

void TSFilterDestroy(TSFilter_t* tsfilter)
{
    tsfilter->quit = TRUE;
    pthread_join(tsfilter->thread, NULL);
    pthread_mutex_destroy(&tsfilter->mutex);

    ListFree(tsfilter->pidFilters, free);
    ObjectRefDec(tsfilter);
}

void TSFilterEnable(TSFilter_t* tsfilter, bool enable)
{
    pthread_mutex_lock(&tsfilter->mutex);
    tsfilter->enabled = enable;
    pthread_mutex_unlock(&tsfilter->mutex);
}

void TSFilterZeroStats(TSFilter_t* tsfilter)
{
    ListIterator_t iterator;
    pthread_mutex_lock(&tsfilter->mutex);
    /* Clear all filter stats */
    tsfilter->totalPackets = 0;
    tsfilter->bitrate = 0;

    for (ListIterator_Init(iterator, tsfilter->pidFilters); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        PIDFilter_t *filter = (PIDFilter_t *)ListIterator_Current(iterator);
        filter->packetsFiltered  = 0;
        filter->packetsProcessed = 0;
        filter->packetsOutput    = 0;
    }
    pthread_mutex_unlock(&tsfilter->mutex);
}

void TSFilterMultiplexChanged(TSFilter_t *tsfilter, Multiplex_t *newmultiplex)
{
    pthread_mutex_lock(&tsfilter->mutex);
    tsfilter->multiplexChanged = TRUE;
    tsfilter->multiplex = newmultiplex;
    pthread_mutex_unlock(&tsfilter->mutex);
}

PIDFilter_t* TSFilterFindPIDFilter(TSFilter_t *tsfilter, const char *name, const char *type)
{
    ListIterator_t iterator;
    PIDFilter_t *result = NULL;
    pthread_mutex_lock(&tsfilter->mutex);
    for (ListIterator_Init(iterator, tsfilter->pidFilters); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        PIDFilter_t *filter = (PIDFilter_t *)ListIterator_Current(iterator);
        if ((strcmp(filter->name, name) == 0) && (strcmp(filter->type, type) == 0))
        {
            result = filter;
            break;
        }
    }
    pthread_mutex_unlock(&tsfilter->mutex);
    return result;
}
/*******************************************************************************
* PID Filter Functions                                                         *
*******************************************************************************/
PIDFilter_t* PIDFilterAllocate(TSFilter_t* tsfilter)
{
    pthread_mutex_lock(&tsfilter->mutex);
    PIDFilter_t *result = calloc(1, sizeof(PIDFilter_t));
    if (result)
    {
        ListAdd(tsfilter->pidFilters, result);
        result->tsFilter = tsfilter;
        result->type = "";
    }
    pthread_mutex_unlock(&tsfilter->mutex);
    return result;
}

void PIDFilterFree(PIDFilter_t * pidfilter)
{
    TSFilter_t *tsfilter = pidfilter->tsFilter;
    pthread_mutex_lock(&tsfilter->mutex);
    ListRemove(pidfilter->tsFilter->pidFilters, pidfilter);
    free(pidfilter);
    pthread_mutex_unlock(&tsfilter->mutex);
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
        filter->filterPacket = filterpacket;
        filter->fpArg = fparg;

        filter->processPacket = processpacket;
        filter->ppArg = pparg;

        filter->outputPacket = outputpacket;
        filter->opArg = oparg;
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


/*******************************************************************************
* Internal Functions                                                           *
*******************************************************************************/
static void *FilterTS(void *arg)
{
    struct timeval now, last;
    int diff;
    unsigned long long prevpackets = 0;
    bool locked = FALSE;

    TSFilter_t *state = (TSFilter_t*)arg;
    DVBAdapter_t *adapter = state->adapter;
    int count = 0;

    DVBDemuxSetBufferSize(adapter, sizeof(state->readBuffer) * 2);

    gettimeofday(&last, 0);

    while (!state->quit)
    {
        int p;
        /* Read in packet */
        count = DVBDVRRead(adapter, (char*)state->readBuffer, sizeof(state->readBuffer), 100);
        if (state->quit)
        {
            break;
        }

        if (state->multiplexChanged)
        {
            InformMultiplexChanged(state);
            state->multiplexChanged = FALSE;
            locked = FALSE;
            /* Thow away these packets as they could be a mix of packets from the old TS and the new TS */
            continue;
        }

        if (!locked)
        {
            fe_status_t status = 0;
            DVBFrontEndStatus(adapter, &status, NULL, NULL, NULL, NULL);

            if (status & FE_HAS_LOCK)
            {
                locked = TRUE;
            }
            continue;
        }

        pthread_mutex_lock(&state->mutex);
        for (p = 0; (p < (count / TSPACKET_SIZE)) && state->enabled; p ++)
        {
            ProcessPacket(state, &state->readBuffer[p]);
            state->totalPackets ++;
            /* The structure of the transport stream has changed in a major way,
                (ie new services, services removed) so inform all of the filters
                that are interested.
              */
            if (state->tsStructureChanged)
            {
                InformTSStructureChanged(state);
                state->tsStructureChanged = FALSE;
            }
        }

        gettimeofday(&now, 0);
        diff =(now.tv_sec - last.tv_sec) * 1000 + (now.tv_usec - last.tv_usec) / 1000;
        if (diff > 1000)
        {
            // Work out bit rates
            state->bitrate = (unsigned long)((state->totalPackets - prevpackets) * (188 * 8));
            prevpackets = state->totalPackets;
            last = now;
        }
        pthread_mutex_unlock(&state->mutex);
    }
    LogModule(LOG_DEBUG, "TSFilter", "Filter thread exiting.\n");
    return NULL;
}

static void ProcessPacket(TSFilter_t *state, TSPacket_t *packet)
{
    uint16_t pid = TSPACKET_GETPID(*packet);

    ListIterator_t iterator;
    if (!TSPACKET_ISVALID(*packet))
    {
        return;
    }
    for (ListIterator_Init(iterator, state->pidFilters); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        PIDFilter_t *filter =(PIDFilter_t *)ListIterator_Current(iterator);
        if (filter->enabled && filter->filterPacket && 
            filter->filterPacket(filter, filter->fpArg, pid, packet))
        {
            bool output = TRUE;
            TSPacket_t *outputPacket = packet;

            filter->packetsFiltered ++;

            if (filter->processPacket)
            {
                outputPacket = filter->processPacket(filter, filter->ppArg, packet);
                output = (outputPacket) ? TRUE:FALSE;
                filter->packetsProcessed ++;
            }

            if (output && filter->outputPacket)
            {
                filter->outputPacket(filter, filter->opArg, outputPacket);
                filter->packetsOutput ++;
            }
        }
    }
}

static void InformTSStructureChanged(TSFilter_t *state)
{
    ListIterator_t iterator;
    for (ListIterator_Init(iterator, state->pidFilters); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        PIDFilter_t *filter =(PIDFilter_t *)ListIterator_Current(iterator);
        if (filter->enabled && filter->tsStructureChanged)
        {
            filter->tsStructureChanged(filter, filter->tscArg);
        }
    }
}

static void InformMultiplexChanged(TSFilter_t *state)
{
    ListIterator_t iterator;
    for (ListIterator_Init(iterator, state->pidFilters); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        PIDFilter_t *filter =(PIDFilter_t *)ListIterator_Current(iterator);
        if (filter->enabled && filter->multiplexChanged)
        {
            filter->multiplexChanged(filter, filter->mcArg, state->multiplex);
        }
    }
}
