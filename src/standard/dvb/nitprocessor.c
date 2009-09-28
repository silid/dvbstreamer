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

#include "ts.h"
#include "logging.h"
#include "nitprocessor.h"
#include "dvbpsi/nit.h"

/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/

struct NITProcessor_s
{
    TSFilterGroup_t *tsgroup;
    dvbpsi_handle demux;    
};
/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void NITProcessorFilterEventCallback(void *userArg, struct TSFilterGroup_t *group, TSFilterEventType_e event, void *details);
static void SubTableHandler(void * state, dvbpsi_handle demuxHandle, uint8_t tableId, uint16_t extension);
static void NITHandler(void* arg, dvbpsi_nit_t* newNIT);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static Event_t nitEvent = NULL;
static char NITPROCESSOR[] = "NITProcessor";

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
NITProcessor_t NITProcessorCreate(TSReader_t *reader)
{
    NITProcessor_t result;
    if (nitEvent == NULL)
    {
        nitEvent = EventsRegisterEvent(DVBEventSource, "nit", NULL);
    }
    ObjectRegisterClass("NITProcessor_t", sizeof(struct NITProcessor_s), NULL);
    result = ObjectCreateType(NITProcessor_t);
    if (result)
    {
        result->tsgroup = TSReaderCreateFilterGroup(reader, NITPROCESSOR, "DVB", NITProcessorFilterEventCallback, result);
    }

    return result;
}

void NITProcessorDestroy(NITProcessor_t processor)
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
static void NITProcessorFilterEventCallback(void *userArg, struct TSFilterGroup_t *group, TSFilterEventType_e event, void *details)
{
    NITProcessor_t state= (NITProcessor_t)userArg;

    if (event == TSFilterEventType_MuxChanged)
    {
        if (state->demux)
        {
            TSFilterGroupRemoveSectionFilter(state->tsgroup, PID_NIT);
            dvbpsi_DetachDemux(state->demux);
        }

        if (details)
        {
            state->demux = dvbpsi_AttachDemux(SubTableHandler, state);
            TSFilterGroupAddSectionFilter(state->tsgroup, PID_NIT, 1, state->demux);
        }
        else
        {
            state->demux = NULL;
        }
    }
}

static void SubTableHandler(void * arg, dvbpsi_handle demuxHandle, uint8_t tableId, uint16_t extension)
{
    if((tableId == TABLE_ID_NIT_ACTUAL) || (tableId == TABLE_ID_NIT_OTHER))
    {
        dvbpsi_AttachNIT(demuxHandle, tableId, extension, NITHandler, arg);
    }
}

static void NITHandler(void* arg, dvbpsi_nit_t* newNIT)
{
    EventsFireEventListeners(nitEvent, newNIT);
    ObjectRefDec(newNIT);
}

