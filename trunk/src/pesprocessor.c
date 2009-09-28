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

pesprocessor.c

Process PES sections.

*/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
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
#include "logging.h"

/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/
typedef struct PESProcessor_t
{
    PIDFilterSimpleFilter_t simplefilter;
    bool payloadStartOnly;
    bool discontinuity;
    int counter;
    uint16_t packetLength;
    uint8_t packet[64 * 1024]; /* Maximum size of a PES packet is 64KB */
    List_t *callbacksList;
}
PESProcessor_t;

typedef struct CallbackDetails_t
{
    PluginPESProcessor_t callback;
    void *userarg;
}CallbackDetails_t;

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static PIDFilter_t *PESProcessorFind(uint16_t pid);
static PIDFilter_t *PESProcessorCreate(TSReader_t *tsfilter, uint16_t pid);
static void PESProcessorDestroy(PIDFilter_t *filter);
static void PESProcessorRegisterCallback(PIDFilter_t *filter,PluginPESProcessor_t callback, void *userarg);
static void PESProcessorUnRegisterCallback(PIDFilter_t *filter,PluginPESProcessor_t callback, void *userarg);
static void PESProcessorMultiplexChanged(PIDFilter_t *pidfilter, void *arg, Multiplex_t *newmultiplex);
static TSPacket_t * PESProcessorProcessPacket(PIDFilter_t *pidfilter, void *arg, TSPacket_t *packet);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static List_t *SectionProcessorsList = NULL;
static const char PESPROCESSOR[] = "PESProcessor";

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
void PESProcessorStartPID(uint16_t pid, PluginPESProcessor_t callback, void *userarg)
{
    PIDFilter_t *processor = PESProcessorFind(pid);

    if (SectionProcessorsList == NULL)
    {
        SectionProcessorsList = ListCreate();
    }

    if (processor == NULL)
    {
        processor = PESProcessorCreate(MainTSReaderGet(), pid);
    }
    if (processor)
    {

        PESProcessorRegisterCallback(processor, callback, userarg);

        if (SectionProcessorsList == NULL)
        {
            SectionProcessorsList = ListCreate();
        }

        ListAdd(SectionProcessorsList, processor);
    }
}

void PESProcessorStopPID(uint16_t pid, PluginPESProcessor_t callback, void *userarg)
{
    PIDFilter_t *processor = PESProcessorFind(pid);
    if (processor == NULL)
    {
        return;
    }

    PESProcessorUnRegisterCallback(processor, callback, userarg);
}


void PESProcessorDestroyAllProcessors(void)
{
    if (SectionProcessorsList == NULL)
    {
        return;
    }

    ListFree(SectionProcessorsList, (ListDataDestructor_t)PESProcessorDestroy);
    SectionProcessorsList = NULL;
}

static PIDFilter_t *PESProcessorFind(uint16_t pid)
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
        PESProcessor_t *processor = (PESProcessor_t *)filter->ppArg;

        if (processor->simplefilter.pids[0] == pid)
        {
            return filter;
        }
    }
    return NULL;
}

static PIDFilter_t *PESProcessorCreate(TSReader_t *tsfilter, uint16_t pid)
{
    PIDFilter_t *result = NULL;
    PESProcessor_t *state;

    ObjectRegisterType(PESProcessor_t);
    state = ObjectCreateType(PESProcessor_t);
    if (state)
    {
        state->simplefilter.pidcount = 1;
        state->simplefilter.pids[0] = pid;
        result = PIDFilterSetup(tsfilter,
                                PIDFilterSimpleFilter, &state->simplefilter,
                                PESProcessorProcessPacket, state,
                                NULL,NULL);
        if (result == NULL)
        {
            ObjectRefDec(state);
        }

        if (asprintf(&result->name, "PES(PID 0x%04x)", pid) == -1)
        {
            LogModule(LOG_INFO, PESPROCESSOR, "Failed to allocate memory for filter name.");
        }

        result->type = "PES";
        result->enabled = TRUE;
        PIDFilterMultiplexChangeSet(result,PESProcessorMultiplexChanged, state);

        state->callbacksList = ListCreate();
    }

    return result;
}

static void PESProcessorDestroy(PIDFilter_t *filter)
{
    PESProcessor_t *state = (PESProcessor_t *)filter->ppArg;
    assert(filter->processPacket == PESProcessorProcessPacket);
    PIDFilterFree(filter);
    ListFree(state->callbacksList, free);
    ObjectRefDec(state);
}

static void PESProcessorRegisterCallback(PIDFilter_t *filter,PluginPESProcessor_t callback, void *userarg)
{
    PESProcessor_t *state = (PESProcessor_t *)filter->ppArg;
    CallbackDetails_t *details;

    assert(filter->processPacket == PESProcessorProcessPacket);

    details = malloc(sizeof(CallbackDetails_t));
    if (details)
    {
        details->callback = callback;
        details->userarg = userarg;
        ListAdd(state->callbacksList, details);
    }
}

static void PESProcessorUnRegisterCallback(PIDFilter_t *filter,PluginPESProcessor_t callback, void *userarg)
{
    PESProcessor_t *state = (PESProcessor_t *)filter->ppArg;
    ListIterator_t iterator;

    assert(filter->processPacket == PESProcessorProcessPacket);

    for (ListIterator_Init(iterator, state->callbacksList);
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

static void PESProcessorMultiplexChanged(PIDFilter_t *pidfilter, void *arg, Multiplex_t *newmultiplex)
{
    PESProcessor_t *state = (PESProcessor_t *)arg;

    if (newmultiplex)
    {
        state->discontinuity = TRUE;
    }
}

static TSPacket_t * PESProcessorProcessPacket(PIDFilter_t *pidfilter, void *arg, TSPacket_t *packet)
{
    PESProcessor_t *state = (PESProcessor_t *)arg;
    uint8_t expectedCounter;
    int payloadStart = 0;

    /* Continuity check */
    expectedCounter = (state->counter + 1) & 0xf;
    state->counter = TSPACKET_GETCOUNT(*packet);

    if (expectedCounter == ((state->counter + 1) & 0xf)
        && !state->discontinuity)
    {
        /* TS duplicate*/
        return NULL;
    }

    if (expectedCounter != state->counter)
    {
        state->discontinuity = TRUE;
    }

    /* Return if no payload in the TS packet */
    if (!(packet->header[3] & 0x10))
    {
        return NULL;
    }

    /* Skip the adaptation_field if present */
    if (packet->header[3] & 0x20)
    {
        payloadStart = 1 + packet->payload[0];
    }

    if (TSPACKET_ISPAYLOADUNITSTART(*packet))
    {
        if (state->packetLength > 0)
        {
            ListIterator_t iterator;

            for (ListIterator_Init(iterator, state->callbacksList);
                 ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
            {
                CallbackDetails_t *details = ListIterator_Current(iterator);
                details->callback(details->userarg, state->packet, state->packetLength);
            }
            state->packetLength = 0;
        }
    }

    memcpy(state->packet + state->packetLength, &packet->payload[payloadStart], 184 - payloadStart);
    state->packetLength += 184 - payloadStart;

    return NULL;
}

