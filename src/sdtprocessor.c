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

sdtprocessor.c

Process Service Description Tables and update the services information.

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

#define TABLE_ID_SDT_ACTUAL 0x42
#define TABLE_ID_SDT_OTHER  0x46

#define DESCRIPTOR_SERVICE  0x48

typedef struct SDTProcessor_t
{
    PIDFilterSimpleFilter_t simplefilter;
    Multiplex_t *multiplex;
    dvbpsi_handle demuxhandle;
    bool payloadstartonly;
}
SDTProcessor_t;
static void SDTProcessorMultiplexChanged(PIDFilter_t *pidfilter, void *arg, Multiplex_t *newmultiplex);
static TSPacket_t * SDTProcessorProcessPacket(PIDFilter_t *pidfilter, void *arg, TSPacket_t *packet);
static void SubTableHandler(void * state, dvbpsi_handle demuxHandle, uint8_t tableId, uint16_t extension);
static void SDTHandler(void* arg, dvbpsi_sdt_t* newSDT);
static List_t *NewSDTCallbacksList = NULL;

PIDFilter_t *SDTProcessorCreate(TSFilter_t *tsfilter)
{
    PIDFilter_t *result = NULL;
    SDTProcessor_t *state = calloc(1, sizeof(SDTProcessor_t));
    if (state)
    {
        state->simplefilter.pidcount = 1;
        state->simplefilter.pids[0] = 0x11;
        result = PIDFilterSetup(tsfilter,
                    PIDFilterSimpleFilter, &state->simplefilter,
                    SDTProcessorProcessPacket, state,
                    NULL,NULL);
        if (result == NULL)
        {
            free(state);
        }
        result->name = "SDT";
        PIDFilterMultiplexChangeSet(result,SDTProcessorMultiplexChanged, state);
    }

    if (!NewSDTCallbacksList)
    {
        NewSDTCallbacksList = ListCreate();
    }

    return result;
}

void SDTProcessorDestroy(PIDFilter_t *filter)
{
    SDTProcessor_t *state = (SDTProcessor_t *)filter->pparg;
    assert(filter->processpacket == SDTProcessorProcessPacket);
    PIDFilterFree(filter);

    if (state->multiplex)
    {
        dvbpsi_DetachDemux(state->demuxhandle);
    }
    free(state);
}

void SDTProcessorRegisterSDTCallback(PluginSDTProcessor_t callback)
{
    if (NewSDTCallbacksList)
    {
        ListAdd(NewSDTCallbacksList, callback);
    }
}

void SDTProcessorUnRegisterSDTCallback(PluginSDTProcessor_t callback)
{
    if (NewSDTCallbacksList)
    {
        ListRemove(NewSDTCallbacksList, callback);
    }
}

static void SDTProcessorMultiplexChanged(PIDFilter_t *pidfilter, void *arg, Multiplex_t *newmultiplex)
{
    SDTProcessor_t *state = (SDTProcessor_t *)arg;
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

static TSPacket_t * SDTProcessorProcessPacket(PIDFilter_t *pidfilter, void *arg, TSPacket_t *packet)
{
    SDTProcessor_t *state = (SDTProcessor_t *)arg;

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
    SDTProcessor_t *state = (SDTProcessor_t *)arg;

    if(tableId == TABLE_ID_SDT_ACTUAL)
    {
        dvbpsi_AttachSDT(demuxHandle, tableId, extension, SDTHandler, state);
    }
}

static void SDTHandler(void* arg, dvbpsi_sdt_t* newSDT)
{
    ListIterator_t iterator;
    dvbpsi_sdt_service_t* sdtservice = newSDT->p_first_service;
    printlog(LOG_DEBUG,"SDT recieved, version %d\n", newSDT->i_version);
    while(sdtservice)
    {
        Service_t *service = CacheServiceFindId(sdtservice->i_service_id);
        if (service)
        {
            dvbpsi_descriptor_t* descriptor = sdtservice->p_first_descriptor;
            while(descriptor)
            {
                if (descriptor->i_tag == DESCRIPTOR_SERVICE)
                {
                    dvbpsi_service_dr_t* servicedesc = dvbpsi_DecodeServiceDr(descriptor);
                    if (servicedesc)
                    {
                        char name[255];
                        int i;
                        for (i = 0; i < servicedesc->i_service_name_length; i ++)
                        {
                            char chr = servicedesc->i_service_name[i];
                            if (FilterServiceNames && !isprint(chr))
                            {
                                chr = FilterReplacementChar;
                            }
                            name[i] = chr;
                        }
                        name[servicedesc->i_service_name_length] = 0;
                        /* Only update the name if it has changed */
                        if (strcmp(name, service->name))
                        {
                            printlog(LOG_DEBUG,"Updating service 0x%04x = %s\n", sdtservice->i_service_id, name);
                            CacheUpdateServiceName(service, name);
                        }
                    }
                    break;
                }
                descriptor = descriptor->p_next;
            }
            service->conditionalaccess = sdtservice->b_free_ca;
            service->runningstatus = sdtservice->i_running_status;
            service->eitpresentfollowing = sdtservice->b_eit_present;
            service->eitschedule = sdtservice->b_eit_schedule;

            ServiceRefDec(service);
        }
        sdtservice =sdtservice->p_next;
    }
    /* Set the Original Network id, this is a hack should really get and decode the NIT */
    if (CurrentMultiplex->netid != newSDT->i_network_id)
    {
        CacheUpdateNetworkId((Multiplex_t *)CurrentMultiplex, newSDT->i_network_id);
    }

    for (ListIterator_Init(iterator, NewSDTCallbacksList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        PluginSDTProcessor_t callback = ListIterator_Current(iterator);
        callback(newSDT);
    }

    dvbpsi_DeleteSDT(newSDT);
}
