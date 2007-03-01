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

nitprocessor.c

Process Network Information Tables.

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
#include "nitprocessor.h"
#include "dvbpsi/nit.h"
#include "dvbpsi/dr_83.h"


#define TABLE_ID_NIT_ACTUAL 0x40
#define TABLE_ID_NIT_OTHER  0x41

typedef struct NITProcessor_t
{
    PIDFilterSimpleFilter_t simplefilter;
    Multiplex_t *multiplex;
    dvbpsi_handle demuxhandle;
    bool payloadstartonly;
}
NITProcessor_t;

static void NITProcessorMultiplexChanged(PIDFilter_t *pidfilter, void *arg, Multiplex_t *newmultiplex);
static TSPacket_t * NITProcessorProcessPacket(PIDFilter_t *pidfilter, void *arg, TSPacket_t *packet);
static void SubTableHandler(void * state, dvbpsi_handle demuxHandle, uint8_t tableId, uint16_t extension);
static void NITHandler(void* arg, dvbpsi_nit_t* newNIT);
static List_t *NewNITCallbacksList = NULL;

PIDFilter_t *NITProcessorCreate(TSFilter_t *tsfilter)
{
    PIDFilter_t *result = NULL;
    NITProcessor_t *state = calloc(1, sizeof(NITProcessor_t));
    if (state)
    {
        state->simplefilter.pidcount = 1;
        state->simplefilter.pids[0] = 0x10;
        result = PIDFilterSetup(tsfilter,
                    PIDFilterSimpleFilter, &state->simplefilter,
                    NITProcessorProcessPacket, state,
                    NULL,NULL);
        if (result == NULL)
        {
            free(state);
        }
        result->name = "NIT";
        PIDFilterMultiplexChangeSet(result,NITProcessorMultiplexChanged, state);
    }

    if (!NewNITCallbacksList)
    {
        NewNITCallbacksList = ListCreate();
    }
    return result;
}

void NITProcessorDestroy(PIDFilter_t *filter)
{
    NITProcessor_t *state = (NITProcessor_t *)filter->pparg;
    assert(filter->processpacket == NITProcessorProcessPacket);
    PIDFilterFree(filter);

    if (state->multiplex)
    {
        dvbpsi_DetachDemux(state->demuxhandle);
    }
    free(state);
}

void NITProcessorRegisterNITCallback(PluginNITProcessor_t callback)
{
    if (NewNITCallbacksList)
    {
        ListAdd(NewNITCallbacksList, callback);
    }
}

void NITProcessorUnRegisterNITCallback(PluginNITProcessor_t callback)
{
    if (NewNITCallbacksList)
    {
        ListRemove(NewNITCallbacksList, callback);
    }
}

static void NITProcessorMultiplexChanged(PIDFilter_t *pidfilter, void *arg, Multiplex_t *newmultiplex)
{
    NITProcessor_t *state = (NITProcessor_t *)arg;
    if (state->multiplex)
    {
        dvbpsi_DetachDemux(state->demuxhandle);
    }
    if (newmultiplex)
    {
        state->demuxhandle = dvbpsi_AttachDemux(SubTableHandler, (void*)state);
        state->payloadstartonly = TRUE;
    }
    state->multiplex = newmultiplex;
}

static TSPacket_t * NITProcessorProcessPacket(PIDFilter_t *pidfilter, void *arg, TSPacket_t *packet)
{
    NITProcessor_t *state = (NITProcessor_t *)arg;

    if (state->multiplex == NULL)
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

static void SubTableHandler(void * arg, dvbpsi_handle demuxHandle, uint8_t tableId, uint16_t extension)
{
    NITProcessor_t *state = (NITProcessor_t *)arg;

    if(tableId == TABLE_ID_NIT_ACTUAL)
    {
        dvbpsi_AttachNIT(demuxHandle, tableId, extension, NITHandler, state);
    }
}

static void NITHandler(void* arg, dvbpsi_nit_t* newNIT)
{
    ListIterator_t iterator;
    dvbpsi_nit_transport_t *transport = newNIT->p_first_transport;
    printlog(LOG_DEBUG, "Network ID = 0x%04x\n", newNIT->i_network_id);
    printlog(LOG_DEBUG, "Version    = %d\n", newNIT->i_version);


    while (transport)
    {
    	dvbpsi_descriptor_t *descriptor = transport->p_first_descriptor;
			
        printlog(LOG_DEBUG, "Transport Stream ID = 0x%04x\n", transport->i_ts_id);
        printlog(LOG_DEBUG, "Original Network ID = 0x%04x\n", transport->i_original_network_id);

	while (descriptor)
	{
		if (descriptor->i_tag == 0x83)
		{
			int i;
			dvbpsi_lcn_dr_t * lcndescriptor = dvbpsi_DecodeLCNDr(descriptor);

			printlog(LOG_DEBUG, "Logical Channel Numbers\n");
			for (i = 0; i < lcndescriptor->i_number_of_entries; i ++)
			{
				printlog(LOG_DEBUG, "%d : %04x (Visible? %s)\n", lcndescriptor->p_entries[i].i_logical_channel_number,
					lcndescriptor->p_entries[i].i_service_id, lcndescriptor->p_entries[i].b_visible_service_flag?"Yes":"No");
			}
		}
		descriptor = descriptor->p_next;
	}
        transport = transport->p_next;
    }

    for (ListIterator_Init(iterator, NewNITCallbacksList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        PluginNITProcessor_t callback = ListIterator_Current(iterator);
        callback(newNIT);
    }
    dvbpsi_DeleteNIT(newNIT);
}

