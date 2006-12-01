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

tdtprocessor.c

Process Time/Date and Time Offset Tables.

*/
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <assert.h>

#include <dvbpsi/dvbpsi.h>
#include <dvbpsi/descriptor.h>
#include <dvbpsi/dr.h>
#include <dvbpsi/demux.h>

#include "multiplexes.h"
#include "services.h"
#include "pids.h"
#include "dvb.h"
#include "ts.h"
#include "main.h"
#include "cache.h"
#include "logging.h"
#include "list.h"
#include "tdtprocessor.h"
#include "dvbpsi/tdttot.h"


#define TABLE_ID_TDT 0x40
#define TABLE_ID_TOT 0x41

typedef struct TDTProcessor_t
{
    PIDFilterSimpleFilter_t simplefilter;
    dvbpsi_handle handle;
    Multiplex_t *multiplex;
    bool payloadstartonly;
}
TDTProcessor_t;

static void TDTProcessorMultiplexChanged(PIDFilter_t *pidfilter, void *arg, Multiplex_t *newmultiplex);
static TSPacket_t * TDTProcessorProcessPacket(PIDFilter_t *pidfilter, void *arg, TSPacket_t *packet);
static void TDTHandler(void* arg, dvbpsi_tdt_tot_t* newTDT);
static List_t *NewTDTCallbacksList = NULL;

PIDFilter_t *TDTProcessorCreate(TSFilter_t *tsfilter)
{
    PIDFilter_t *result = NULL;
    TDTProcessor_t *state = calloc(1, sizeof(TDTProcessor_t));
    if (state)
    {
        state->simplefilter.pidcount = 1;
        state->simplefilter.pids[0] = 0x14;
        result = PIDFilterSetup(tsfilter,
                    PIDFilterSimpleFilter, &state->simplefilter,
                    TDTProcessorProcessPacket, state,
                    NULL,NULL);
        if (result == NULL)
        {
            free(state);
        }
        PIDFilterMultiplexChangeSet(result,TDTProcessorMultiplexChanged, state);
    }

    if (!NewTDTCallbacksList)
    {
        NewTDTCallbacksList = ListCreate();
    }
    return result;
}

void TDTProcessorDestroy(PIDFilter_t *filter)
{
    TDTProcessor_t *state = (TDTProcessor_t *)filter->pparg;
    assert(filter->processpacket == TDTProcessorProcessPacket);
    PIDFilterFree(filter);

    if (state->multiplex)
    {
        dvbpsi_DetachTDTTOT(state->handle);
    }
    free(state);
}

void TDTProcessorRegisterTDTCallback(PluginTDTProcessor_t callback)
{
    if (NewTDTCallbacksList)
    {
        ListAdd(NewTDTCallbacksList, callback);
    }
}

void TDTProcessorUnRegisterTDTCallback(PluginTDTProcessor_t callback)
{
    if (NewTDTCallbacksList)
    {
        ListRemove(NewTDTCallbacksList, callback);
    }
}

static void TDTProcessorMultiplexChanged(PIDFilter_t *pidfilter, void *arg, Multiplex_t *newmultiplex)
{
    TDTProcessor_t *state = (TDTProcessor_t *)arg;
    if (state->multiplex)
    {
        dvbpsi_DetachTDTTOT(state->handle);
    }
    if (newmultiplex)
    {
        state->handle = dvbpsi_AttachTDTTOT(TDTHandler, (void*)state);
        state->payloadstartonly = TRUE;
    }
    state->multiplex = newmultiplex;
}

static TSPacket_t * TDTProcessorProcessPacket(PIDFilter_t *pidfilter, void *arg, TSPacket_t *packet)
{
    TDTProcessor_t *state = (TDTProcessor_t *)arg;

    if (state->multiplex == NULL)
    {
        return 0;
    }

    if (state->payloadstartonly)
    {
        if (TSPACKET_ISPAYLOADUNITSTART(*packet))
        {
            state->handle->i_continuity_counter = (TSPACKET_GETCOUNT(*packet) - 1) & 0xf;
            dvbpsi_PushPacket(state->handle, (uint8_t*)packet);
            state->payloadstartonly = FALSE;
        }
    }
    else
    {
        dvbpsi_PushPacket(state->handle, (uint8_t*)packet);
    }

    return 0;
}

static void TDTHandler(void* arg, dvbpsi_tdt_tot_t* newTDT)
{
    ListIterator_t iterator;

    printlog(LOG_DEBUG, "%s: %2d/%2d/%4d %02d:%02d:%02d\n", newTDT->p_first_descriptor ? "TOT":"TDT",
        newTDT->i_day, newTDT->i_month, newTDT->i_year, newTDT->i_hour, newTDT->i_minute, newTDT->i_second);

    for (ListIterator_Init(iterator, NewTDTCallbacksList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        PluginTDTProcessor_t callback = ListIterator_Current(iterator);
        callback(newTDT);
    }
    dvbpsi_DeleteTDTTOT(newTDT);
}


