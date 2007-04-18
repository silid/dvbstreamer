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
#define _GNU_SOURCE

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

typedef struct CallbackDetails_t
{
    PluginSectionProcessor_t callback;
    void *userarg;
}CallbackDetails_t;

static PIDFilter_t *SectionProcessorFind(uint16_t pid);
static PIDFilter_t *SectionProcessorCreate(TSFilter_t *tsfilter, uint16_t pid);
static void SectionProcessorDestroy(PIDFilter_t *filter);
static void SectionProcessorRegisterSectionCallback(PIDFilter_t *filter,PluginSectionProcessor_t callback, void *userarg);
static void SectionProcessorUnRegisterSectionCallback(PIDFilter_t *filter,PluginSectionProcessor_t callback, void *userarg);
static void SectionProcessorMultiplexChanged(PIDFilter_t *pidfilter, void *arg, Multiplex_t *newmultiplex);
static TSPacket_t * SectionProcessorProcessPacket(PIDFilter_t *pidfilter, void *arg, TSPacket_t *packet);
static void SectionHandler(dvbpsi_decoder_t* p_decoder, dvbpsi_psi_section_t* newSection);

static List_t *SectionProcessorsList = NULL;

void SectionProcessorStartPID(uint16_t pid, PluginSectionProcessor_t callback, void *userarg)
{
    PIDFilter_t *processor = SectionProcessorFind(pid);
    if (processor == NULL)
    {
        processor = SectionProcessorCreate(TSFilter, pid);
    }
    if (processor)
    {
        SectionProcessorRegisterSectionCallback(processor, callback, userarg);

        if (SectionProcessorsList == NULL)
        {
            SectionProcessorsList = ListCreate();
        }

        ListAdd(SectionProcessorsList, processor);
    }
}

void SectionProcessorStopPID(uint16_t pid, PluginSectionProcessor_t callback, void *userarg)
{
    PIDFilter_t *processor = SectionProcessorFind(pid);
    if (processor == NULL)
    {
        return;
    }

    SectionProcessorUnRegisterSectionCallback(processor, callback, userarg);
}


void SectionProcessorDestroyAllProcessors(void)
{
    if (SectionProcessorsList == NULL)
    {
        return;
    }

    ListFree(SectionProcessorsList, (ListDataDestructor_t)SectionProcessorDestroy);
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
        SectionProcessor_t *processor = (SectionProcessor_t *)filter->ppArg;

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
    SectionProcessor_t *state;
    
    ObjectRegisterType(SectionProcessor_t);
    state = ObjectCreateType(SectionProcessor_t);
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
            ObjectRefDec(state);
        }
        asprintf( &result->name, "Section(PID 0x%04x)", pid);
        PIDFilterMultiplexChangeSet(result,SectionProcessorMultiplexChanged, state);
    }

    return result;
}

static void SectionProcessorDestroy(PIDFilter_t *filter)
{
    SectionProcessor_t *state = (SectionProcessor_t *)filter->ppArg;
    assert(filter->processPacket == SectionProcessorProcessPacket);
    PIDFilterFree(filter);

    if (state->handle)
    {
        free(state->handle);
    }

    ListFree(state->sectioncallbackslist, free);
    ObjectRefDec(state);
}

static void SectionProcessorRegisterSectionCallback(PIDFilter_t *filter,PluginSectionProcessor_t callback, void *userarg)
{
    SectionProcessor_t *state = (SectionProcessor_t *)filter->ppArg;
    CallbackDetails_t *details;
    
    assert(filter->processPacket == SectionProcessorProcessPacket);
    
    details = malloc(sizeof(CallbackDetails_t));
    if (details)
    {
        details->callback = callback;
        details->userarg = userarg;
        ListAdd(state->sectioncallbackslist, details);
    }
}

static void SectionProcessorUnRegisterSectionCallback(PIDFilter_t *filter,PluginSectionProcessor_t callback, void *userarg)
{
    SectionProcessor_t *state = (SectionProcessor_t *)filter->ppArg;
    ListIterator_t iterator;
    assert(filter->processPacket == SectionProcessorProcessPacket);

    for (ListIterator_Init(iterator, state->sectioncallbackslist);
         ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        CallbackDetails_t *details = ListIterator_Current(iterator);
        if ((details->callback == callback) && (details->userarg == userarg))
        {
            ListRemoveCurrent( &iterator);
            free(details);
            break;
        }
    }
}

static void SectionProcessorMultiplexChanged(PIDFilter_t *pidfilter, void *arg, Multiplex_t *newmultiplex)
{
    SectionProcessor_t *state = (SectionProcessor_t *)arg;
    if (state->handle)
    {
        if (state->handle->p_current_section)
        {
            dvbpsi_DeletePSISections(state->handle->p_current_section);
        }
        if (state->handle->p_free_sections)
        {
            dvbpsi_DeletePSISections(state->handle->p_free_sections);
        }
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
    SectionProcessor_t *state = (SectionProcessor_t *)filter->ppArg;

    ListIterator_t iterator;

    for (ListIterator_Init(iterator, state->sectioncallbackslist);
            ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        CallbackDetails_t *details = ListIterator_Current(iterator);
        details->callback(details->userarg, newSection);
    }
    dvbpsi_ReleasePSISections(decoder, newSection);
}


