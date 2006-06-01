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
    Multiplex_t *multiplex;
    dvbpsi_handle demuxhandle;
}
SDTProcessor_t;

static void SubTableHandler(void * state, dvbpsi_handle demuxHandle, uint8_t tableId, uint16_t extension);
static void SDTHandler(void* arg, dvbpsi_sdt_t* newSDT);

void *SDTProcessorCreate()
{
    SDTProcessor_t *result = calloc(1, sizeof(SDTProcessor_t));
    return result;
}

void SDTProcessorDestroy(void *arg)
{
    SDTProcessor_t *state = (SDTProcessor_t *)arg;
    if (state->multiplex)
    {
        dvbpsi_DetachDemux(state->demuxhandle);
    }
    free(state);
}


TSPacket_t * SDTProcessorProcessPacket(PIDFilter_t *pidfilter, void *arg, TSPacket_t *packet)
{
    SDTProcessor_t *state = (SDTProcessor_t *)arg;

    if (CurrentMultiplex == NULL)
    {
        return 0;
    }

    if (state->multiplex != CurrentMultiplex)
    {
        if (state->multiplex)
        {
            dvbpsi_DetachDemux(state->demuxhandle);
        }
        if (CurrentMultiplex)
        {
            state->demuxhandle = dvbpsi_AttachDemux(SubTableHandler, (void*)state);
        }
        state->multiplex = (Multiplex_t*)CurrentMultiplex;
    }

    dvbpsi_PushPacket(state->demuxhandle, (uint8_t*)packet);

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
    dvbpsi_sdt_service_t* sdtservice = newSDT->p_first_service;
    printlog(LOG_DEBUG,"SDT recieved, version %d\n", newSDT->i_version);
    while(sdtservice)
    {
        dvbpsi_descriptor_t* descriptor = sdtservice->p_first_descriptor;
        while(descriptor)
        {
            if (descriptor->i_tag == DESCRIPTOR_SERVICE)
            {
                dvbpsi_service_dr_t* servicedesc = dvbpsi_DecodeServiceDr(descriptor);
                if (servicedesc)
                {
                    Service_t *service = CacheServiceFindId(sdtservice->i_service_id);
                    if (service)
                    {
                        char name[255];
                        memcpy(name, servicedesc->i_service_name, servicedesc->i_service_name_length);
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
            }
            descriptor = descriptor->p_next;
        }
        sdtservice =sdtservice->p_next;
    }
    dvbpsi_DeleteSDT(newSDT);
}
