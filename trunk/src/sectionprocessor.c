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

sectionprocessor.c

Process PSI/SI sections.

*/
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <assert.h>

#include <dvbpsi/dvbpsi.h>
#include <dvbpsi/psi.h>

#include "types.h"
#include "list.h"
#include "main.h"
#include "ts.h"
#include "plugin.h"
#include "multiplexes.h"

typedef struct SectionProcessor_t
{
    PIDFilterSimpleFilter_t simplefilter;
    dvbpsi_handle handle;
    bool payloadstartonly;
    List_t *sectioncallbackslist;
}
SectionProcessor_t;

static PIDFilter_t *SectionProcessorFind(uint16_t pid);
static PIDFilter_t *SectionProcessorCreate(TSFilter_t *tsfilter, uint16_t pid);
static void SectionProcessorDestroy(PIDFilter_t *filter);
static void SectionProcessorRegisterSectionCallback(PIDFilter_t *filter,PluginSectionProcessor_t callback);
static void SectionProcessorUnRegisterSectionCallback(PIDFilter_t *filter,PluginSectionProcessor_t callback);
static void SectionProcessorMultiplexChanged(PIDFilter_t *pidfilter, void *arg, Multiplex_t *newmultiplex);
static TSPacket_t * SectionProcessorProcessPacket(PIDFilter_t *pidfilter, void *arg, TSPacket_t *packet);
static void SectionHandler(dvbpsi_decoder_t* p_decoder, dvbpsi_psi_section_t* newSection);

static List_t *SectionProcessorsList = NULL;

void SectionProcessorStartPID(uint16_t pid, PluginSectionProcessor_t callback)
{
    PIDFilter_t *processor = SectionProcessorFind(pid);
    if (processor == NULL)
    {
        processor = SectionProcessorCreate(TSFilter, pid);
    }
    if (processor)
    {

        SectionProcessorRegisterSectionCallback(processor, callback);

        if (SectionProcessorsList == NULL)
        {
            SectionProcessorsList = ListCreate();
        }

        ListAdd(SectionProcessorsList, processor);
    }
}

void SectionProcessorStopPID(uint16_t pid, PluginSectionProcessor_t callback)
{
    PIDFilter_t *processor = SectionProcessorFind(pid);
    if (processor == NULL)
    {
        return;
    }

    SectionProcessorUnRegisterSectionCallback(processor, callback);
}


void SectionProcessorDestroyAllProcessors(void)
{
    ListIterator_t iterator;
    if (SectionProcessorsList == NULL)
    {
        return;
    }
    for (ListIterator_Init(iterator, SectionProcessorsList);
            ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        PIDFilter_t * filter = (PIDFilter_t*)ListIterator_Current(iterator);
        SectionProcessorDestroy(filter);
    }
    ListFree(SectionProcessorsList);
    SectionProcessorsList = NULL;
}

static PIDFilter_t *SectionProcessorFind(uint16_t pid)
{
    ListIterator_t iterator;
    if (SectionProcessorsList == NULL)
    {
        return NULL;
    }
    for (ListIterator_Init(iterator, SectionProcessorsList);
            ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        PIDFilter_t * filter = (PIDFilter_t*)ListIterator_Current(iterator);
        SectionProcessor_t *processor = (SectionProcessor_t *)filter->pparg;

        if (processor->simplefilter.pids[0] == pid)
        {
            return filter;
        }
    }
    return NULL;
}

static PIDFilter_t *SectionProcessorCreate(TSFilter_t *tsfilter, uint16_t pid)
{
    PIDFilter_t *result = NULL;
    SectionProcessor_t *state = calloc(1, sizeof(SectionProcessor_t));
    if (state)
    {
        state->simplefilter.pidcount = 1;
        state->simplefilter.pids[0] = pid;
        result = PIDFilterSetup(tsfilter,
                    PIDFilterSimpleFilter, &state->simplefilter,
                    SectionProcessorProcessPacket, state,
                    NULL,NULL);
        if (result == NULL)
        {
            free(state);
        }
        asprintf( &result->name, "Section(PID 0x%04x)", pid);
        PIDFilterMultiplexChangeSet(result,SectionProcessorMultiplexChanged, state);
    }

    return result;
}

static void SectionProcessorDestroy(PIDFilter_t *filter)
{
    SectionProcessor_t *state = (SectionProcessor_t *)filter->pparg;
    assert(filter->processpacket == SectionProcessorProcessPacket);
    PIDFilterFree(filter);

    if (state->handle)
    {
        free(state->handle);
    }
    ListFree(state->sectioncallbackslist);
    free(state);
}

static void SectionProcessorRegisterSectionCallback(PIDFilter_t *filter,PluginSectionProcessor_t callback)
{
    SectionProcessor_t *state = (SectionProcessor_t *)filter->pparg;
    assert(filter->processpacket == SectionProcessorProcessPacket);

    ListAdd(state->sectioncallbackslist, callback);
}

static void SectionProcessorUnRegisterSectionCallback(PIDFilter_t *filter,PluginSectionProcessor_t callback)
{
    SectionProcessor_t *state = (SectionProcessor_t *)filter->pparg;
    assert(filter->processpacket == SectionProcessorProcessPacket);

    ListRemove(state->sectioncallbackslist, callback);
}

static bool SectionProcessorHasCallbacks(PIDFilter_t *filter)
{
    SectionProcessor_t *state = (SectionProcessor_t *)filter->pparg;
    return (ListCount(state->sectioncallbackslist) > 0);
}

static void SectionProcessorMultiplexChanged(PIDFilter_t *pidfilter, void *arg, Multiplex_t *newmultiplex)
{
    SectionProcessor_t *state = (SectionProcessor_t *)arg;
    if (state->handle)
    {
        free(state->handle);
        state->handle = NULL;
    }

    if (newmultiplex)
    {
        state->handle = (dvbpsi_decoder_t*)malloc(sizeof(dvbpsi_decoder_t));

        if(state->handle == NULL)
        {
            /* PSI decoder configuration */
            state->handle->pf_callback = &SectionHandler;
            state->handle->p_private_decoder = pidfilter;
            state->handle->i_section_max_size = 4096;
            /* PSI decoder initial state */
            state->handle->i_continuity_counter = 31;
            state->handle->b_discontinuity = 1;
            state->handle->p_current_section = NULL;
        }
        state->payloadstartonly = TRUE;
    }
}

static TSPacket_t * SectionProcessorProcessPacket(PIDFilter_t *pidfilter, void *arg, TSPacket_t *packet)
{
    SectionProcessor_t *state = (SectionProcessor_t *)arg;

    if (state->handle == NULL)
    {
        return 0;
    }

    if (state->payloadstartonly)
    {
        if (TSPACKET_ISPAYLOADUNITSTART(*packet))
        {
            state->handle->i_continuity_counter = (TSPACKET_GETCOUNT(*packet) - 1) & 0xf;
            dvbpsi_PushPacket(state->handle, (uint8_t*)packet);
            state->payloadstartonly = FALSE;
        }
    }
    else
    {
        dvbpsi_PushPacket(state->handle, (uint8_t*)packet);
    }

    return 0;
}

static void SectionHandler(dvbpsi_decoder_t* decoder, dvbpsi_psi_section_t* newSection)
{
    PIDFilter_t * filter = (PIDFilter_t*)decoder->p_private_decoder;
    SectionProcessor_t *state = (SectionProcessor_t *)filter->pparg;

    ListIterator_t iterator;

    for (ListIterator_Init(iterator, state->sectioncallbackslist);
            ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        PluginSectionProcessor_t callback = ListIterator_Current(iterator);
        callback(newSection);
    }
    dvbpsi_ReleasePSISections(newSection);
}

