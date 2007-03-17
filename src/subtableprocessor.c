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

subtableprocessor.c

Generic Processor for PSI/SI tables that have several subtables on the same PID.

*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <assert.h>

#include <dvbpsi/dvbpsi.h>
#include <dvbpsi/descriptor.h>
#include <dvbpsi/dr.h>
#include <dvbpsi/demux.h>
#include <dvbpsi/sdt.h>

#include "multiplexes.h"
#include "services.h"
#include "dvb.h"
#include "ts.h"
#include "main.h"
#include "cache.h"
#include "logging.h"
#include "subtableprocessor.h"

typedef struct SubTableProcessor_t
{
    PIDFilterSimpleFilter_t simplefilter;
    dvbpsi_handle demuxhandle;
    bool payloadstartonly;
    MultiplexChanged multiplexchanged;
    void *mcarg;
    dvbpsi_demux_new_cb_t subtablehandler;
    void *stharg;
}
SubTableProcessor_t;

static void SubTableProcessorMultiplexChanged(PIDFilter_t *pidfilter, void *arg, Multiplex_t *newmultiplex);
static TSPacket_t * SubTableProcessorProcessPacket(PIDFilter_t *pidfilter, void *arg, TSPacket_t *packet);



PIDFilter_t *SubTableProcessorCreate(TSFilter_t *tsfilter, uint16_t pid, 
                                           dvbpsi_demux_new_cb_t subtablehandler, void *stharg, 
                                           MultiplexChanged multiplexchanged, void *mcarg)
{
    PIDFilter_t *result = PIDFilterAllocate(tsfilter);

    if (result)
    {
        if (!SubTableProcessorInit(result, pid, subtablehandler, stharg, multiplexchanged, mcarg))
        {
            PIDFilterFree(result);
            result = NULL;
        }
    }

    return result;
}

void SubTableProcessorDestroy(PIDFilter_t *filter)
{
    SubTableProcessorDeinit(filter);
    PIDFilterFree(filter);

}

bool SubTableProcessorInit(PIDFilter_t *filter, uint16_t pid, 
                                   dvbpsi_demux_new_cb_t subtablehandler, void *stharg, 
                                   MultiplexChanged multiplexchanged, void *mcarg)
{
    SubTableProcessor_t *state = calloc(1, sizeof(SubTableProcessor_t));
    if (state)
    {
        state->simplefilter.pidcount = 1;
        state->simplefilter.pids[0] = pid;

        PIDFilterFilterPacketSet(filter, PIDFilterSimpleFilter, &state->simplefilter);
        PIDFilterProcessPacketSet(filter, SubTableProcessorProcessPacket, state);
        PIDFilterMultiplexChangeSet(filter,SubTableProcessorMultiplexChanged, state);

        state->subtablehandler = subtablehandler;
        state->stharg = stharg;
        state->multiplexchanged = multiplexchanged;
        state->mcarg = mcarg;
    }
    
    return (state != NULL);
}

void SubTableProcessorDeinit(PIDFilter_t *filter)
{
    SubTableProcessor_t *state = (SubTableProcessor_t *)filter->ppArg;
    assert(filter->processPacket == SubTableProcessorProcessPacket);

    if (state->demuxhandle)
    {
        dvbpsi_DetachDemux(state->demuxhandle);
    }
    free(state);
}

void *SubTableProcessorGetSubTableHandlerArg(PIDFilter_t *filter)
{
    SubTableProcessor_t *state = (SubTableProcessor_t *)filter->ppArg;
    assert(filter->processPacket == SubTableProcessorProcessPacket);
    return state->stharg;
}

static void SubTableProcessorMultiplexChanged(PIDFilter_t *pidfilter, void *arg, Multiplex_t *newmultiplex)
{
    SubTableProcessor_t *state = (SubTableProcessor_t *)arg;
    if (state->demuxhandle)
    {
        dvbpsi_DetachDemux(state->demuxhandle);
    }

    if (newmultiplex)
    {
        state->demuxhandle = dvbpsi_AttachDemux(state->subtablehandler, state->stharg);
        state->payloadstartonly = TRUE;
    }
    else
    {
        state->demuxhandle = NULL;
    }
    
    if (state->multiplexchanged)
    {
        state->multiplexchanged(pidfilter, state->mcarg, newmultiplex);
    }
}

static TSPacket_t * SubTableProcessorProcessPacket(PIDFilter_t *pidfilter, void *arg, TSPacket_t *packet)
{
    SubTableProcessor_t *state = (SubTableProcessor_t *)arg;

    if (state->demuxhandle == NULL)
    {
        return 0;
    }

    if (state->payloadstartonly)
    {
        if (TSPACKET_ISPAYLOADUNITSTART(*packet))
        {
            state->demuxhandle->i_continuity_counter = (TSPACKET_GETCOUNT(*packet) - 1) & 0xf;
            dvbpsi_PushPacket(state->demuxhandle, (uint8_t*)packet);
            state->payloadstartonly = FALSE;
        }
    }
    else
    {
        dvbpsi_PushPacket(state->demuxhandle, (uint8_t*)packet);
    }

    return 0;
}

