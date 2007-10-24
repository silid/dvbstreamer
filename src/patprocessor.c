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

patprocessor.c

Process Program Association Tables and update the services information.

*/
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

#include "multiplexes.h"
#include "services.h"
#include "dvb.h"
#include "ts.h"
#include "patprocessor.h"
#include "cache.h"
#include "logging.h"
#include "main.h"
#include "list.h"


/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/
typedef struct PATProcessor_t
{
    TSFilter_t *tsfilter;
    PIDFilterSimpleFilter_t simplefilter;
    Multiplex_t *multiplex;
    dvbpsi_handle pathandle;
    bool payloadstartonly;
}
PATProcessor_t;

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void PATProcessorMultiplexChanged(PIDFilter_t *pidfilter, void *arg, Multiplex_t *newmultiplex);
static TSPacket_t *PATProcessorProcessPacket(PIDFilter_t *pidfilter, void *arg, TSPacket_t *packet);
static void PATHandler(void* arg, dvbpsi_pat_t* newpat);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static char PATPROCESSOR[] = "PATProcessor";
static List_t *NewPATCallbacksList = NULL;

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
PIDFilter_t *PATProcessorCreate(TSFilter_t *tsfilter)
{
    PIDFilter_t *result = NULL;
    PATProcessor_t *state;
    ObjectRegisterType(PATProcessor_t);
    state = ObjectCreateType(PATProcessor_t);
    if (state)
    {
        state->simplefilter.pidcount = 1;
        state->simplefilter.pids[0] = 0;
        state->tsfilter = tsfilter;
        result =  PIDFilterSetup(tsfilter,
                    PIDFilterSimpleFilter, &state->simplefilter,
                    PATProcessorProcessPacket, state,
                    NULL,NULL);
        if (result == NULL)
        {
            free(state);
        }
        result->name = "PAT";
        result->type = PSISIPIDFilterType;
        if (tsfilter->adapter->hardwareRestricted)
        {
            DVBDemuxAllocateFilter(tsfilter->adapter, 0, TRUE);
        }
            
        PIDFilterMultiplexChangeSet(result, PATProcessorMultiplexChanged, state);
    }
    if (!NewPATCallbacksList)
    {
        NewPATCallbacksList = ListCreate();
    }
    return result;
}

void PATProcessorDestroy(PIDFilter_t *filter)
{
    PATProcessor_t *state= (PATProcessor_t*)filter->ppArg;
    assert(filter->processPacket == PATProcessorProcessPacket);

    if (filter->tsFilter->adapter->hardwareRestricted)
    {
        DVBDemuxReleaseFilter(filter->tsFilter->adapter, 0);
    }
        
    PIDFilterFree(filter);
    if (state->multiplex)
    {
        dvbpsi_DetachPAT(state->pathandle);
        MultiplexRefDec(state->multiplex);
    }
    ObjectRefDec(state);

    if (NewPATCallbacksList)
    {
        ListFree(NewPATCallbacksList, NULL);
    }
}

void PATProcessorRegisterPATCallback(PluginPATProcessor_t callback)
{
    if (NewPATCallbacksList)
    {
        ListAdd(NewPATCallbacksList, callback);
    }
}

void PATProcessorUnRegisterPATCallback(PluginPATProcessor_t callback)
{
    if (NewPATCallbacksList)
    {
        ListRemove(NewPATCallbacksList, callback);
    }
}

/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/
static void PATProcessorMultiplexChanged(PIDFilter_t *pidfilter, void *arg, Multiplex_t *newmultiplex)
{
    PATProcessor_t *state= (PATProcessor_t*)arg;
    if (state->multiplex)
    {
        MultiplexRefDec(state->multiplex);
        dvbpsi_DetachPAT(state->pathandle);
    }
    state->multiplex = newmultiplex;
    if (newmultiplex)
    {
        MultiplexRefInc(state->multiplex);
        state->pathandle = dvbpsi_AttachPAT(PATHandler, (void*)state);
        state->payloadstartonly = TRUE;
    }
}

static TSPacket_t *PATProcessorProcessPacket(PIDFilter_t *pidfilter, void *arg, TSPacket_t *packet)
{
    TSPacket_t *result = NULL;
    PATProcessor_t *state= (PATProcessor_t*)arg;

    if (state->multiplex)
    {
        if (state->payloadstartonly)
        {
            if (TSPACKET_ISPAYLOADUNITSTART(*packet))
            {
                state->pathandle->i_continuity_counter = (TSPACKET_GETCOUNT(*packet) - 1) & 0xf;
                dvbpsi_PushPacket(state->pathandle, (uint8_t*)packet);
                state->payloadstartonly = FALSE;
            }
        }
        else
        {
            dvbpsi_PushPacket(state->pathandle, (uint8_t*)packet);
        }
    }

    return result;
}

static void PATHandler(void* arg, dvbpsi_pat_t* newpat)
{
    PATProcessor_t *state = (PATProcessor_t*)arg;
    Multiplex_t *multiplex = state->multiplex;
    ListIterator_t iterator;
    int count,i;
    Service_t **services;

    LogModule(LOG_DEBUG, PATPROCESSOR, "PAT recieved, version %d (old version %d)\n", newpat->i_version, multiplex->patVersion);
    if (multiplex->patVersion == -1)
    {
        /* Cause a TS Structure change call back*/
        state->tsfilter->tsStructureChanged = TRUE;
    }
    /* Version has changed update the services */
    dvbpsi_pat_program_t *patentry = newpat->p_first_program;
    while(patentry)
    {
        if (patentry->i_number != 0x0000)
        {
            Service_t *service = CacheServiceFindId(patentry->i_number);
            if (!service)
            {
                LogModule(LOG_DEBUG, PATPROCESSOR, "Service not found in cache while processing PAT, adding 0x%04x\n", patentry->i_number);
                service = CacheServiceAdd(patentry->i_number);
                /* Cause a TS Structure change call back*/
                state->tsfilter->tsStructureChanged = TRUE;
            }

            if (service && (service->pmtPid != patentry->i_pid))
            {
                CacheUpdateService(service, patentry->i_pid);
            }

            if (service)
            {
                ServiceRefDec(service);
            }
        }
        patentry = patentry->p_next;
    }

    /* Delete any services that no longer exist */
    services = CacheServicesGet(&count);
    for (i = 0; i < count; i ++)
    {
        bool found = FALSE;
        for (patentry = newpat->p_first_program; patentry; patentry = patentry->p_next)
        {
            if (services[i]->id == patentry->i_number)
            {
                found = TRUE;
                break;
            }
        }
        if (!found)
        {
            LogModule(LOG_DEBUG, PATPROCESSOR, "Service not found in PAT while checking cache, deleting 0x%04x (%s)\n",
                services[i]->id, services[i]->name);
            CacheServicesRelease();
            CacheServiceDelete(services[i]);
            services = CacheServicesGet(&count);
            i --;
            /* Cause a TS Structure change call back*/
            state->tsfilter->tsStructureChanged = TRUE;
        }
    }

    CacheServicesRelease();
    CacheUpdateMultiplex(multiplex, newpat->i_version, newpat->i_ts_id);

    for (ListIterator_Init(iterator, NewPATCallbacksList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        PluginPATProcessor_t callback = ListIterator_Current(iterator);
        callback(newpat);
    }
    dvbpsi_DeletePAT(newpat);
}
