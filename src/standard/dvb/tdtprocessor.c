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

tdtprocessor.c

Process Time/Date and Time Offset Tables.

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
#include "dvbadapter.h"
#include "ts.h"
#include "main.h"
#include "cache.h"
#include "logging.h"
#include "list.h"
#include "tdtprocessor.h"
#include "dvbpsi/tdttot.h"

/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/

struct TDTProcessor_s
{
    TSFilterGroup_t *tsgroup;
    dvbpsi_handle handle;
};

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/

static void TDTProcessorFilterEventCallback(void *userArg, struct TSFilterGroup_t *group, TSFilterEventType_e event, void *details);
static void TDTHandler(void* arg, dvbpsi_tdt_tot_t* newTDT);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static Event_t tdtEvent = NULL;
static char TDTPROCESSOR[] = "TDTProcessor";
/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
TDTProcessor_t TDTProcessorCreate(TSReader_t *reader)
{
    PIDFilter_t *result = NULL;
    TDTProcessor_t *state;
    ObjectRegisterClass("TDTProcessor_t", sizeof(struct TDTProcessor_s), NULL);
    state = ObjectCreateType(TDTProcessor_t);
    if (state)
    {
        state->tsgroup = TSReaderCreateFilterGroup(reader, TDTPROCESSOR, "dvb", TDTProcessorFilterEventCallback, state);
    }

    return result;
}

void TDTProcessorDestroy(TDTProcessor_t processor)
{
    TSFilterGroupDestroy(processor->tsgroup);
    if (processor->hande)
    {
        dvbpsi_DetachTDTTOT(processor->handle);
    }
    ObjectRefDec(processor);
}

/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/
static void TDTProcessorFilterEventCallback(void *userArg, struct TSFilterGroup_t *group, TSFilterEventType_e event, void *details)
{
    TDTProcessor_t state = (TDTProcessor_t)userArg;
    if (event == TSFilterEventType_MuxChanged)
    {
        if (state->handle)
        {
            TSFilterGroupRemoveSectionFilter(state->tsgroup, PID_TDT);
            dvbpsi_DetachTDTTOT(state->handle);
        }
        if (details)
        {
            state->handle = dvbpsi_AttachTDTTOT(TDTHandler, state);
            TSFilterGroupAddSectionFilter(state->tsgroup, PID_TDT, 2, state->handle);
        }
        else
        {
            state->handle = NULL;
        }
    }
}

static void TDTHandler(void* arg, dvbpsi_tdt_tot_t* newTDT)
{
    ListIterator_t iterator;

    EventsFireEventListeners(tdtEvent, newTDT);
    ObjectRefDec(newTDT);
}


