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

#include "multiplexes.h"
#include "services.h"
#include "dvbadapter.h"
#include "ts.h"
#include "main.h"
#include "cache.h"
#include "logging.h"
#include "sdtprocessor.h"
#include "subtableprocessor.h"
#include "dvbtext.h"

/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/
#define SDT_PID 0x11

#define TABLE_ID_SDT_ACTUAL 0x42
#define TABLE_ID_SDT_OTHER  0x46

#define DESCRIPTOR_SERVICE  0x48
#define DESCRIPTOR_DEFAUTH  0x73

/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/

typedef struct SDTProcessorState_s
{
    TSFilter_t *tsfilter;    
    Multiplex_t *multiplex;
}SDTProcessorState_t;

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void SubTableHandler(void * state, dvbpsi_handle demuxHandle, uint8_t tableId, uint16_t extension);
static void SDTHandler(void* arg, dvbpsi_sdt_t* newSDT);
static void SDTMultiplexChanged(PIDFilter_t *filter, void *arg, Multiplex_t *multiplex);
static void SDTTSStructureChanged(PIDFilter_t *filter, void *arg);
static ServiceType ConvertDVBServiceType(int type);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static char SDTPROCESSOR[] = "SDTProcessor";
static List_t *NewSDTCallbacksList = NULL;

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
int SDTProcessorInit(void)
{
    NewSDTCallbacksList = ListCreate();
    return NewSDTCallbacksList ? 0: -1;
}

void SDTProcessorDeInit(void)
{
    ListFree(NewSDTCallbacksList, NULL);
}

PIDFilter_t *SDTProcessorCreate(TSFilter_t *tsfilter)
{
    SDTProcessorState_t *state = NULL;
    PIDFilter_t *result = NULL;
    ObjectRegisterType(SDTProcessorState_t);
    state = ObjectCreateType(SDTProcessorState_t);
    if (state)
    {
        state->tsfilter = tsfilter;
        result = SubTableProcessorCreate(tsfilter, SDT_PID, SubTableHandler, state, SDTMultiplexChanged, state);

        if (result)
        {
            result->name = "SDT";
            result->type = PSISIPIDFilterType;
            /* If the PAT changes we want to pick up the SDT again as otherwise we 
               may have services with no names.
               Seen this with the UK multiplexes where there have been PAT changes 
               in quick succesion but there has been no change to the SDT. This 
               resulted in the services being available but losing there names
               as the first changed to the PAT removed all services and then the 
               next one added them all back (?!)
            */
            PIDFilterTSStructureChangeSet(result, SDTTSStructureChanged, state);
            if (tsfilter->adapter->hardwareRestricted)
            {
                DVBDemuxAllocateFilter(tsfilter->adapter, SDT_PID, TRUE);
            }
        }
        else
        {
            ObjectRefDec(state);
        }
    }
    return result;
}

void SDTProcessorDestroy(PIDFilter_t *filter)
{
    SDTProcessorState_t *state = filter->tscArg;
    if (filter->tsFilter->adapter->hardwareRestricted)
    {
        DVBDemuxReleaseFilter(filter->tsFilter->adapter, SDT_PID);
    }
    
    SubTableProcessorDestroy(filter);
    MultiplexRefDec(state->multiplex);
    ObjectRefDec(state);
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

/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/
static void SubTableHandler(void * arg, dvbpsi_handle demuxHandle, uint8_t tableId, uint16_t extension)
{
    if(tableId == TABLE_ID_SDT_ACTUAL)
    {
        dvbpsi_AttachSDT(demuxHandle, tableId, extension, SDTHandler, arg);
    }
}

static void SDTHandler(void* arg, dvbpsi_sdt_t* newSDT)
{
    SDTProcessorState_t *state = arg;
    ListIterator_t iterator;
    dvbpsi_sdt_service_t* sdtservice = newSDT->p_first_service;
    int count,i;
    Service_t **services;
    
    LogModule(LOG_DEBUG, SDTPROCESSOR, "SDT recieved, version %d\n", newSDT->i_version);
    while(sdtservice)
    {
        dvbpsi_descriptor_t* descriptor = sdtservice->p_first_descriptor;
        bool ca;
        Service_t *service = CacheServiceFindId(sdtservice->i_service_id);
        
        if (!service)
        {
            service = CacheServiceAdd(sdtservice->i_service_id);
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
                state->tsfilter->tsStructureChanged = TRUE;
            }
        }
    }
    CacheServicesRelease();
    
    /* Set the Original Network id, this is a hack should really get and decode the NIT */
    if (state->multiplex->networkId != newSDT->i_network_id)
    {
        CacheUpdateNetworkId(state->multiplex, newSDT->i_network_id);
    }

    for (ListIterator_Init(iterator, NewSDTCallbacksList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        PluginSDTProcessor_t callback = ListIterator_Current(iterator);
        callback(newSDT);
    }

    ObjectRefDec(newSDT);
}

static void SDTMultiplexChanged(PIDFilter_t *filter, void *arg, Multiplex_t *multiplex)
{
    SDTProcessorState_t *state = arg;
    MultiplexRefDec(state->multiplex);
    state->multiplex = multiplex;
    MultiplexRefInc(state->multiplex);
}

static void SDTTSStructureChanged(PIDFilter_t *filter, void *arg)
{
    SDTProcessorState_t *state = arg;
    filter->multiplexChanged(filter, filter->mcArg, state->multiplex);
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
