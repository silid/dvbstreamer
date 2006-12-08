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
#include "dvb.h"
#include "ts.h"
#include "main.h"
#include "cache.h"
#include "logging.h"
#include "list.h"
#include "pmtprocessor.h"

#include <dvbpsi/dr_0a.h>

#define MAX_HANDLES 256

typedef struct PMTProcessor_t
{
    Multiplex_t   *multiplex;
    Service_t     *services[MAX_HANDLES];
    unsigned short pmtpids[MAX_HANDLES];
    dvbpsi_handle  pmthandles[MAX_HANDLES];
    bool           payloadstartonly[MAX_HANDLES];
}
PMTProcessor_t;

static int PMTProcessorFilterPacket(PIDFilter_t *pidfilter, void *arg, uint16_t pid, TSPacket_t *packet);
static void PMTProcessorMultiplexChanged(PIDFilter_t *pidfilter, void *arg, Multiplex_t *newmultiplex);
static TSPacket_t *PMTProcessorProcessPacket(PIDFilter_t *pidfilter, void *arg, TSPacket_t *packet);
static void PMTProcessorTSStructureChanged(PIDFilter_t *pidfilter, void *arg);
static void PMTHandler(void* arg, dvbpsi_pmt_t* newpmt);


static List_t *NewPMTCallbacksList = NULL;

PIDFilter_t *PMTProcessorCreate(TSFilter_t *tsfilter)
{
    PIDFilter_t *result = NULL;
    PMTProcessor_t *state = calloc(1, sizeof(PMTProcessor_t));
    if (state)
    {
        result =  PIDFilterSetup(tsfilter,
                    PMTProcessorFilterPacket, state,
                    PMTProcessorProcessPacket, state,
                    NULL,NULL);
        if (!result)
        {
            free(state);
        }
        result->name = "PMT";
        PIDFilterTSStructureChangeSet(result, PMTProcessorTSStructureChanged, state);
        PIDFilterMultiplexChangeSet(result, PMTProcessorMultiplexChanged, state);
    }
    if (!NewPMTCallbacksList)
    {
        NewPMTCallbacksList = ListCreate();
    }
    return result;
}

void PMTProcessorDestroy(PIDFilter_t *filter)
{
    PMTProcessor_t *state = (PMTProcessor_t *)filter->fparg;
    int i;
    assert(filter->filterpacket == PMTProcessorFilterPacket);
    PIDFilterFree(filter);

    for (i = 0; i < MAX_HANDLES; i ++)
    {
        if (state->pmthandles[i])
        {
            dvbpsi_DetachPMT(state->pmthandles[i]);
            state->pmtpids[i]    = 0;
            state->pmthandles[i] = NULL;
            state->services[i]   = NULL;
        }
    }
    free(state);
    if (NewPMTCallbacksList)
    {
        ListFree(NewPMTCallbacksList);
    }
}

void PMTProcessorRegisterPMTCallback(PluginPMTProcessor_t callback)
{
    if (NewPMTCallbacksList)
    {
        ListAdd(NewPMTCallbacksList, callback);
    }
}

void PMTProcessorUnRegisterPMTCallback(PluginPMTProcessor_t callback)
{
    if (NewPMTCallbacksList)
    {
        ListRemove(NewPMTCallbacksList, callback);
    }
}


static int PMTProcessorFilterPacket(PIDFilter_t *pidfilter, void *arg, uint16_t pid, TSPacket_t *packet)
{
    if (CurrentMultiplex)
    {
        int i;
        int count;
        Service_t **services;

        services = CacheServicesGet(&count);
        for (i = 0; i < count; i ++)
        {
            if (pid == services[i]->pmtpid)
            {
                return 1;
            }
        }
    }
    return 0;
}

static void PMTProcessorMultiplexChanged(PIDFilter_t *pidfilter, void *arg, Multiplex_t *newmultiplex)
{
    PMTProcessor_t *state = (PMTProcessor_t *)arg;
    int count;
    int i;
    Service_t **services;

    for (i = 0; i < MAX_HANDLES; i ++)
    {
        if (state->pmthandles[i])
        {
            dvbpsi_DetachPMT(state->pmthandles[i]);
            state->pmtpids[i]    = 0;
            state->pmthandles[i] = NULL;
            state->services[i]   = NULL;
        }
    }

    services = CacheServicesGet(&count);
    if (count > MAX_HANDLES)
    {
        printlog(LOG_ERROR,"Too many services in TS, cannot monitor them all only monitoring %d out of %d\n", MAX_HANDLES, count);
        count = MAX_HANDLES;
    }
    for (i = 0; i < count; i ++)
    {
        state->pmtpids[i] = services[i]->pmtpid;
        state->services[i] = services[i];
        state->pmthandles[i] = dvbpsi_AttachPMT(services[i]->id, PMTHandler, (void*)services[i]);
        state->payloadstartonly[i] = TRUE;
    }
    state->multiplex = (Multiplex_t*)newmultiplex;
}

static TSPacket_t * PMTProcessorProcessPacket(PIDFilter_t *pidfilter, void *arg, TSPacket_t *packet)
{
    TSPacket_t *result = NULL;
    PMTProcessor_t *state = (PMTProcessor_t *)arg;
    unsigned short pid = TSPACKET_GETPID(*packet);
    int i;

    if (state->multiplex == NULL)
    {
        return 0;
    }

    for (i = 0; i < MAX_HANDLES; i ++)
    {
        if (state->pmthandles[i] && state->pmtpids[i] == pid)
        {
            /* Different program PMTs may be on the same so pass the packet to
               all handlers that match the pid
            */
            if (state->payloadstartonly[i])
            {
                if (TSPACKET_ISPAYLOADUNITSTART(*packet))
                {
                    state->pmthandles[i]->i_continuity_counter = (TSPACKET_GETCOUNT(*packet) - 1) & 0xf;
                    dvbpsi_PushPacket(state->pmthandles[i], (uint8_t*)packet);
                    state->payloadstartonly[i] = FALSE;
                }
            }
            else
            {
                dvbpsi_PushPacket(state->pmthandles[i], (uint8_t*)packet);
            }
        }
    }

    return result;
}

static void PMTProcessorTSStructureChanged(PIDFilter_t *pidfilter, void *arg)
{
    PMTProcessor_t *state = (PMTProcessor_t *)arg;
#if 0
    int i,count;
    Service_t **services;
    services = CacheServicesGet(&count);

    for (i = 0; i < MAX_HANDLES; i ++)
    {

        if (state->pmthandles[i])
        {
            int found = 0;
            int s;
            for (s = 0; s < count; s ++)
            {
                if (ServiceAreEqual(state->services[i], services[s]))
                {
                    found = 1;
                    break;
                }
            }
            if (!found)
            {
                dvbpsi_DetachPMT(state->pmthandles[i]);
                state->pmtpids[i]    = 0;
                state->pmthandles[i] = NULL;
                state->services[i]   = NULL;
            }
        }
    }

    /* Look for services that we are currently not monitoring ie 'new' services */
    for (i = 0; i < count; i ++)
    {
        int found = 0;
        int emptyIndex = -1;
        int index;
        for (index = 0; index < MAX_HANDLES; index++)
        {
            if (state->pmthandles[index] == NULL)
            {
                if (emptyIndex == -1)
                {
                    emptyIndex = i;
                }
                continue;
            }
            if ((state->pmthandles[index] != NULL) && ServiceAreEqual(state->services[index], services[i]))
            {
                found = 1;
                break;
            }
        }
        if (!found && (emptyIndex != -1))
        {
            state->pmtpids[emptyIndex] = services[i]->pmtpid;
            state->services[emptyIndex] = services[i];
            state->pmthandles[emptyIndex] = dvbpsi_AttachPMT(services[i]->id, PMTHandler, (void*)services[i]);
        }
    }
#else
    printlog(LOG_DEBUG,"PMTProcessor: TS Structure changed!\n");
    PMTProcessorMultiplexChanged(pidfilter, state, state->multiplex);
#endif
}

static void PMTHandler(void* arg, dvbpsi_pmt_t* newpmt)
{
    Service_t *service = (Service_t*)arg;
    ListIterator_t iterator;
    PIDList_t *pids;
    dvbpsi_pmt_es_t *esentry = newpmt->p_first_es;
    int count = 0;

    printlog(LOG_DEBUG,"PMT recieved, version %d on PID %d (old version %d)\n", newpmt->i_version, service->pmtpid, service->pmtversion);

    while(esentry)
    {
        esentry = esentry->p_next;
        count ++;
    }
    printlog(LOG_DEBUGV,"%d PIDs in PMT\n", count);
    pids = PIDListNew(count);

    if (pids)
    {
        int i;
        esentry = newpmt->p_first_es;

        for (i = 0; i < count; i ++)
        {
            printlog(LOG_DEBUGV, "0x%04x %d\n", esentry->i_pid, esentry->i_type);
            pids->pids[i].pid = esentry->i_pid;
            pids->pids[i].type = esentry->i_type;
            pids->pids[i].descriptors = esentry->p_first_descriptor;

            if ((esentry->i_type == 3) || (esentry->i_type == 4))
            {
                dvbpsi_descriptor_t *desc = esentry->p_first_descriptor;
                while(desc)
                {
                    printlog(LOG_DEBUGV,"Descriptor %d\n", desc->i_tag);
                    if (desc->i_tag == 10) /* ISO 639 Language Descriptor */
                    {
                        dvbpsi_iso639_dr_t *iso639 = dvbpsi_DecodeISO639Dr(desc);
                        pids->pids[i].subtype = iso639->i_audio_type;
                    }
                    desc = desc->p_next;
                }
            }
            else
            {
                pids->pids[i].subtype = 0;
            }
            esentry = esentry->p_next;
        }
        printlog(LOG_DEBUGV,"About to update cache\n");
        CacheUpdatePIDs(service, newpmt->i_pcr_pid, pids, newpmt->i_version);
    }

    for (ListIterator_Init(iterator, NewPMTCallbacksList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        PluginPMTProcessor_t callback = ListIterator_Current(iterator);
        callback(newpmt);
    }

    /* Take over the descriptors */
    esentry = newpmt->p_first_es;
    while(esentry)
    {
        esentry->p_first_descriptor = NULL;
        esentry = esentry->p_next;
    }
    dvbpsi_DeletePMT(newpmt);
}
