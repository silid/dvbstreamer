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
#include <ctype.h>
#include <unistd.h>
#include <stdint.h>
#include <assert.h>

#include <dvbpsi/dvbpsi.h>
#include <dvbpsi/descriptor.h>
#include <dvbpsi/dr.h>
#include <dvbpsi/demux.h>
#include <dvbpsi/sdt.h>

#include "events.h"
#include "multiplexes.h"
#include "services.h"
#include "dvbadapter.h"
#include "ts.h"
#include "main.h"
#include "cache.h"
#include "logging.h"
#include "tuning.h"
#include "sdtprocessor.h"
#include "dvbtext.h"
#include "standard/dvb.h"

/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/
#define PID_SDT 0x11

#define TABLE_ID_SDT_ACTUAL 0x42
#define TABLE_ID_SDT_OTHER  0x46

#define DESCRIPTOR_SERVICE  0x48
#define DESCRIPTOR_DEFAUTH  0x73

/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/

struct SDTProcessor_s
{
    TSFilterGroup_t *tsgroup;    
    dvbpsi_handle demux;
};

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void SDTProcessorFilterEventCallback(void *userArg, struct TSFilterGroup_t *group, TSFilterEventType_e event, void *details);
static void SubTableHandler(void * state, dvbpsi_handle demuxHandle, uint8_t tableId, uint16_t extension);
static void SDTHandler(void* arg, dvbpsi_sdt_t* newSDT);
static ServiceType ConvertDVBServiceType(int type);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static char SDTPROCESSOR[] = "SDTProcessor";
static Event_t sdtEvent = NULL;

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
SDTProcessor_t SDTProcessorCreate(TSReader_t *reader)
{
    SDTProcessor_t state = NULL;
    if (sdtEvent == NULL)
    {
        sdtEvent = EventsRegisterEvent(DVBEventSource, "SDT", NULL);
    }
    ObjectRegisterClass("SDTProcessor_t", sizeof(struct SDTProcessor_s), NULL);
    state = ObjectCreateType(SDTProcessor_t);
    if (state)
    {
        state->tsgroup = TSReaderCreateFilterGroup(reader, SDTPROCESSOR, "DVB", SDTProcessorFilterEventCallback, state);
    }
    return state;
}

void SDTProcessorDestroy(SDTProcessor_t processor)
{
    TSFilterGroupDestroy(processor->tsgroup);
    if (processor->demux)
    {
        dvbpsi_DetachDemux(processor->demux);
    }
    ObjectRefDec(processor);
}

/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/
static void SDTProcessorFilterEventCallback(void *userArg, struct TSFilterGroup_t *group, TSFilterEventType_e event, void *details)
{
    SDTProcessor_t state = (SDTProcessor_t)userArg;
    if (state->demux)
    {
        TSFilterGroupRemoveSectionFilter(state->tsgroup, PID_SDT);
        dvbpsi_DetachDemux(state->demux);
    }

    state->demux = dvbpsi_AttachDemux(SubTableHandler, state);
    TSFilterGroupAddSectionFilter(state->tsgroup, PID_SDT, 1, state->demux);
}

static void SubTableHandler(void * arg, dvbpsi_handle demuxHandle, uint8_t tableId, uint16_t extension)
{
    if(tableId == TABLE_ID_SDT_ACTUAL)
    {
        dvbpsi_AttachSDT(demuxHandle, tableId, extension, SDTHandler, arg);
    }
}

static void SDTHandler(void* arg, dvbpsi_sdt_t* newSDT)
{
    SDTProcessor_t state = arg;
    dvbpsi_sdt_service_t* sdtservice = newSDT->p_first_service;
    int count,i;
    Service_t **services;
    Multiplex_t *mux;
    
    LogModule(LOG_DEBUG, SDTPROCESSOR, "SDT recieved, version %d\n", newSDT->i_version);
    while(sdtservice)
    {
        dvbpsi_descriptor_t* descriptor = sdtservice->p_first_descriptor;
        bool ca;
        Service_t *service = CacheServiceFindId(sdtservice->i_service_id);
        
        if (!service)
        {
            service = CacheServiceAdd(sdtservice->i_service_id, sdtservice->i_service_id);
        }
        else
        {
            CacheServiceSeen(service, TRUE, FALSE);
        }
        
        while(descriptor)
        {
            if (descriptor->i_tag == DESCRIPTOR_SERVICE)
            {
                dvbpsi_service_dr_t* servicedesc = dvbpsi_DecodeServiceDr(descriptor);
                if (servicedesc)
                {
                    char *name;
                    ServiceType type = ConvertDVBServiceType(servicedesc->i_service_type);
                    
                    name = DVBTextToUTF8((char *)servicedesc->i_service_name, servicedesc->i_service_name_length);

                    if (name)
                    {
                        /* Only update the name if it has changed */
                        if (strcmp(name, service->name))
                        {
                            LogModule(LOG_DEBUG, SDTPROCESSOR, "Updating service 0x%04x = %s\n", sdtservice->i_service_id, name);
                            CacheUpdateServiceName(service, name);
                        }
                        free(name);
                    }
                    name = DVBTextToUTF8((char *)servicedesc->i_service_provider_name, servicedesc->i_service_provider_name_length);
                    if (name)
                    {
                        if ((service->provider==NULL) || strcmp(name, service->provider))
                        {
                            LogModule(LOG_DEBUG, SDTPROCESSOR, "Updating service provider 0x%04x = %s\n", sdtservice->i_service_id, name);
                            CacheUpdateServiceProvider(service, name);                        
                        }
                        free(name);
                    }
                    if (service->type != type)
                    {
                        CacheUpdateServiceType(service, type);
                    }
                }
            }
            if (descriptor->i_tag == DESCRIPTOR_DEFAUTH)
            {
                dvbpsi_default_authority_dr_t *defAuthDesc = dvbpsi_DecodeDefaultAuthorityDr(descriptor);

                if (defAuthDesc)
                {
                    if ((service->defaultAuthority == NULL) || strcmp((char*)defAuthDesc->authority, service->defaultAuthority))
                    {
                        LogModule(LOG_DEBUG, SDTPROCESSOR, "Updating service default authority 0x%04x = %s\n", sdtservice->i_service_id, defAuthDesc->authority);
                        CacheUpdateServiceDefaultAuthority(service, (char*)defAuthDesc->authority);                        
                    }
                }
            }
            descriptor = descriptor->p_next;
        }
        ca = sdtservice->b_free_ca ? TRUE:FALSE;
        if (service->conditionalAccess != ca)
        {
            CacheUpdateServiceConditionalAccess(service,ca);
        }
        
        ServiceRefDec(service);

        sdtservice =sdtservice->p_next;
    }

    /* Delete any services that no longer exist */
    services = CacheServicesGet(&count);
    for (i = 0; i < count; i ++)
    {
        bool found = FALSE;
        for (sdtservice = newSDT->p_first_service; sdtservice; sdtservice = sdtservice->p_next)
        {
            if (services[i]->id == sdtservice->i_service_id)
            {
                found = TRUE;
                break;
            }
        }
        if (!found)
        {
            LogModule(LOG_DEBUG, SDTPROCESSOR, "Service not found in SDT while checking cache, deleting 0x%04x (%s)\n",
                services[i]->id, services[i]->name);
            if (!CacheServiceSeen(services[i], FALSE, FALSE))
            {
                CacheServicesRelease();
                CacheServiceDelete(services[i]);
                services = CacheServicesGet(&count);
                i --;
                /* Cause a TS Structure change call back*/
                state->tsgroup->tsReader->tsStructureChanged = TRUE;
            }
        }
    }
    CacheServicesRelease();
    mux = TuningCurrentMultiplexGet();
    /* Set the Original Network id, this is a hack should really get and decode the NIT */
    if (mux->networkId != newSDT->i_network_id)
    {
        CacheUpdateNetworkId(mux, newSDT->i_network_id);
    }
    MultiplexRefDec(mux);
    
    EventsFireEventListeners(sdtEvent, newSDT);
    ObjectRefDec(newSDT);
}

static ServiceType ConvertDVBServiceType(int type)
{
    ServiceType result = ServiceType_Unknown;
    switch(type)
    {
        case 1: 
            result = ServiceType_TV;
            break;
        case 2:
            result = ServiceType_Radio;
            break;
        case 3:
        case 12:
        case 16:
            result = ServiceType_Data;
            break;
    }
    return result;
}
