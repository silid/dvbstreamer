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

pmtprocessor.c

Process Program Map Tables and update the services information and PIDs.

*/
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <assert.h>

#include "multiplexes.h"
#include "services.h"
#include "pids.h"
#include "dvbadapter.h"
#include "ts.h"
#include "main.h"
#include "cache.h"
#include "logging.h"
#include "list.h"
#include "standard/mpeg2.h"
#include "pmtprocessor.h"

#include <dvbpsi/dr_0a.h>

/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/

#define MAX_HANDLES 256

/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/

struct PMTProcessor_s
{
    TSFilterGroup_t *tsgroup;
    Service_t     *services[MAX_HANDLES];
    unsigned short pmtpids[MAX_HANDLES];
    dvbpsi_handle  pmthandles[MAX_HANDLES];
};

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void PMTProcessorFilterEventCallback(void *userArg, struct TSFilterGroup_t *group, TSFilterEventType_e event, void *details);
static void PMTHandler(void* arg, dvbpsi_pmt_t* newpmt);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/

static char PMTPROCESSOR[] = "PMTProcessor";
static Event_t pmtEvent = NULL;

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/

PMTProcessor_t PMTProcessorCreate(TSReader_t *reader)
{
    PMTProcessor_t state;
    if (pmtEvent == NULL)
    {
        pmtEvent = EventsRegisterEvent(MPEG2EventSource, "PMT", NULL);
    }
    ObjectRegisterClass("PMTProcessor_t", sizeof(struct PMTProcessor_s), NULL);
    state = ObjectCreateType(PMTProcessor_t);
    if (state)
    {
        state->tsgroup = TSReaderCreateFilterGroup(reader, PMTPROCESSOR, MPEG2FilterType, PMTProcessorFilterEventCallback, state);
    }
    return state;
}

void PMTProcessorDestroy(PMTProcessor_t processor)
{
    int i;
    TSFilterGroupDestroy(processor->tsgroup);
    for (i = 0; i < MAX_HANDLES; i ++)
    {
        if (processor->pmthandles[i])
        {
            dvbpsi_DetachPMT(processor->pmthandles[i]);
            processor->pmthandles[i] = NULL;
            ServiceRefDec(processor->services[i]);
            processor->services[i]   = NULL;
        }
    }
    ObjectRefDec(processor);
}

/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/
static void PMTProcessorFilterEventCallback(void *userArg, struct TSFilterGroup_t *group, TSFilterEventType_e event, void *details)
{
    PMTProcessor_t state = (PMTProcessor_t)userArg;
    int count;
    int i;
    Service_t **services;

    TSFilterGroupRemoveAllFilters(state->tsgroup);

    for (i = 0; i < MAX_HANDLES; i ++)
    {
        if (state->pmthandles[i])
        {
            dvbpsi_DetachPMT(state->pmthandles[i]);
            state->pmthandles[i] = NULL;
            ServiceRefDec(state->services[i]);
            state->services[i]   = NULL;
        }
    }

    
    services = CacheServicesGet(&count);
    if (count > MAX_HANDLES)
    {
        LogModule(LOG_ERROR, PMTPROCESSOR, "Too many services in TS, cannot monitor them all only monitoring %d out of %d\n", MAX_HANDLES, count);
        count = MAX_HANDLES;
    }
    for (i = 0; i < count; i ++)
    {
        ServiceRefInc(services[i]);
        state->services[i] = services[i];
        state->pmthandles[i] = dvbpsi_AttachPMT(services[i]->id, PMTHandler, (void*)services[i]);
        TSFilterGroupAddSectionFilter(state->tsgroup, services[i]->pmtPid, 0, state->pmthandles[i]);
    }
    CacheServicesRelease();    
}

static void PMTHandler(void* arg, dvbpsi_pmt_t* newpmt)
{
    Service_t *service = (Service_t*)arg;
    ProgramInfo_t *info;
    dvbpsi_pmt_es_t *esentry = newpmt->p_first_es;
    int count = 0;

    LogModule(LOG_DEBUG, PMTPROCESSOR, "PMT recieved, version %d on PID %d (old version %d)\n", newpmt->i_version, service->pmtPid, service->pmtVersion);

    EventsFireEventListeners(pmtEvent, newpmt);
    
    while(esentry)
    {
        esentry = esentry->p_next;
        count ++;
    }
    LogModule(LOG_DEBUGV, PMTPROCESSOR, "%d PIDs in PMT\n", count);
    info = ProgramInfoNew(count);

    if (info)
    {
        int i;
        info->pcrPID = newpmt->i_pcr_pid;
        info->descriptors = newpmt->p_first_descriptor;
        newpmt->p_first_descriptor = NULL;
        esentry = newpmt->p_first_es;

        for (i = 0; i < count; i ++)
        {
            LogModule(LOG_DEBUGV, PMTPROCESSOR, "    %u %d\n", esentry->i_pid, esentry->i_type);
            info->streamInfoList->streams[i].pid = esentry->i_pid;
            info->streamInfoList->streams[i].type = esentry->i_type;
            info->streamInfoList->streams[i].descriptors = esentry->p_first_descriptor;
            {
                dvbpsi_descriptor_t *desc = esentry->p_first_descriptor;
                while(desc)
                {
                    LogModule(LOG_DEBUGV, PMTPROCESSOR, "        Descriptor 0x%02x %u\n", desc->i_tag, desc->i_length);
                    desc = desc->p_next;
                }                
            }
            esentry->p_first_descriptor = NULL; /* Take over the descriptors */
            esentry = esentry->p_next;
        }
        LogModule(LOG_DEBUGV,PMTPROCESSOR, "About to update cache\n");
        CacheUpdateProgramInfo(service, info);
    }

    ObjectRefDec(newpmt);
}

