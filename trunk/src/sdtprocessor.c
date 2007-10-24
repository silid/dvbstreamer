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
#define _GNU_SOURCE
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
#include "dvb.h"
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

/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/

typedef struct SDTProcessorState_s
{
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
    SDTProcessorState_t *state;
    PIDFilter_t *result;
    ObjectRegisterType(SDTProcessorState_t);
    state = ObjectCreateType(SDTProcessorState_t);
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

                    /* Only update the name if it has changed */
                    if (strcmp(name, service->name))
                    {
                        LogModule(LOG_DEBUG, SDTPROCESSOR, "Updating service 0x%04x = %s\n", sdtservice->i_service_id, name);
                        CacheUpdateServiceName(service, name);
                    }
                    if (service->type != type)
                    {
                        CacheUpdateServiceType(service, type);
                    }
                }
                break;
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

    dvbpsi_DeleteSDT(newSDT);
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
