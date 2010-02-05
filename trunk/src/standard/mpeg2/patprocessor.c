/*
Copyright (C) 2009  Adam Charrett

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
#include "dvbadapter.h"
#include "ts.h"
#include "standard/mpeg2.h"
#include "patprocessor.h"
#include "cache.h"
#include "logging.h"
#include "main.h"
#include "list.h"


/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/
struct PATProcessor_s
{
    TSFilterGroup_t *tsgroup;
    Multiplex_t *multiplex;
    dvbpsi_handle pathandle;
};

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void PATProcessorFilterEventCallback(void *userArg, struct TSFilterGroup_t *group, TSFilterEventType_e event, void *details);
static void PATHandler(void* arg, dvbpsi_pat_t* newpat);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static char PATPROCESSOR[] = "PATProcessor";
static Event_t patEvent = NULL;

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
PATProcessor_t PATProcessorCreate(TSReader_t *reader)
{
    PATProcessor_t state;
    if (patEvent == NULL)
    {
        patEvent = EventsRegisterEvent(MPEG2EventSource, "PAT", NULL);
    }
    
    ObjectRegisterClass("PATProcessor_t", sizeof(struct PATProcessor_s), NULL);
    state = ObjectCreateType(PATProcessor_t);
    if (state)
    {
        state->tsgroup = TSReaderCreateFilterGroup(reader, PATPROCESSOR, MPEG2FilterType, PATProcessorFilterEventCallback, state);
    }
    return state;
}

void PATProcessorDestroy(PATProcessor_t processor)
{
    TSFilterGroupDestroy(processor->tsgroup);

    if (processor->multiplex)
    {
        dvbpsi_DetachPAT(processor->pathandle);
        MultiplexRefDec(processor->multiplex);
    }
    ObjectRefDec(processor);
}

/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/
static void PATProcessorFilterEventCallback(void *userArg, struct TSFilterGroup_t *group, TSFilterEventType_e event, void *details)
{
    PATProcessor_t state= (PATProcessor_t)userArg;

    if (event == TSFilterEventType_MuxChanged)
    {
        if (state->multiplex)
        {
            MultiplexRefDec(state->multiplex);
            TSFilterGroupRemoveSectionFilter(state->tsgroup, 0);
            dvbpsi_DetachPAT(state->pathandle);
        }
        state->multiplex = details;
        if (details)
        {
            MultiplexRefInc(state->multiplex);
            state->pathandle = dvbpsi_AttachPAT(PATHandler, (void*)state);
            TSFilterGroupAddSectionFilter(state->tsgroup, 0, -1, state->pathandle);
        }
        
    }
}

static void PATHandler(void* arg, dvbpsi_pat_t* newpat)
{
    PATProcessor_t state = (PATProcessor_t)arg;
    Multiplex_t *multiplex = state->multiplex;
    int count,i;
    Service_t **services;
    dvbpsi_pat_program_t *patentry;
    
    LogModule(LOG_DEBUG, PATPROCESSOR, "PAT recieved, version %d (old version %d)\n", newpat->i_version, multiplex->patVersion);
    if (multiplex->patVersion == -1)
    {
        /* Cause a TS Structure change call back*/
        state->tsgroup->tsReader->tsStructureChanged = TRUE;
    }
    /* Version has changed update the services */

    for (patentry = newpat->p_first_program; patentry; patentry = patentry->p_next)
    {
        LogModule(LOG_DEBUG, PATPROCESSOR, "Service 0x%04x PMT PID 0x%04x\n", patentry->i_number, patentry->i_pid);
        if (patentry->i_number != 0x0000)
        {
            Service_t *service = CacheServiceFindId(patentry->i_number);
            if (!service)
            {
                LogModule(LOG_DEBUG, PATPROCESSOR, "Service not found in cache while processing PAT, adding 0x%04x\n", patentry->i_number);
                service = CacheServiceAdd(patentry->i_number, patentry->i_number);
                /* Cause a TS Structure change call back*/
                state->tsgroup->tsReader->tsStructureChanged = TRUE;
            }
            else
            {
                CacheServiceSeen(service, TRUE, TRUE);
            }

            if (service && (service->pmtPid != patentry->i_pid))
            {
                CacheUpdateServicePMTPID(service, patentry->i_pid);
            }

            if (service)
            {
                ServiceRefDec(service);
            }
        }
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
            if (!CacheServiceSeen(services[i], FALSE, TRUE))
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
    CacheUpdateMultiplex(multiplex, newpat->i_version, newpat->i_ts_id);

    EventsFireEventListeners(patEvent, newpat);
    ObjectRefDec(newpat);
}

