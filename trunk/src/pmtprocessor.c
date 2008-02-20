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

/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/

#define MAX_HANDLES 256

/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/

typedef struct PMTProcessor_t
{
    Multiplex_t   *multiplex;
    Service_t     *services[MAX_HANDLES];
    unsigned short pmtpids[MAX_HANDLES];
    dvbpsi_handle  pmthandles[MAX_HANDLES];
    bool           payloadstartonly[MAX_HANDLES];
}
PMTProcessor_t;

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/

static int PMTProcessorFilterPacket(PIDFilter_t *pidfilter, void *arg, uint16_t pid, TSPacket_t *packet);
static void PMTProcessorMultiplexChanged(PIDFilter_t *pidfilter, void *arg, Multiplex_t *newmultiplex);
static TSPacket_t *PMTProcessorProcessPacket(PIDFilter_t *pidfilter, void *arg, TSPacket_t *packet);
static void PMTProcessorTSStructureChanged(PIDFilter_t *pidfilter, void *arg);
static void PMTHandler(void* arg, dvbpsi_pmt_t* newpmt);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/

static char PMTPROCESSOR[] = "PMTProcessor";
static List_t *NewPMTCallbacksList = NULL;

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/

PIDFilter_t *PMTProcessorCreate(TSFilter_t *tsfilter)
{
    PIDFilter_t *result = NULL;
    PMTProcessor_t *state;
    ObjectRegisterType(PMTProcessor_t);
    state = ObjectCreateType(PMTProcessor_t);
    if (state)
    {
        result =  PIDFilterSetup(tsfilter,
                    PMTProcessorFilterPacket, state,
                    PMTProcessorProcessPacket, state,
                    NULL,NULL);
        if (!result)
        {
            ObjectRefDec(state);
        }
        result->name = "PMT";
        result->type = PSISIPIDFilterType;
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
    PMTProcessor_t *state = (PMTProcessor_t *)filter->fpArg;
    int i;
    assert(filter->filterPacket == PMTProcessorFilterPacket);
    PIDFilterFree(filter);

    for (i = 0; i < MAX_HANDLES; i ++)
    {
        if (state->pmthandles[i])
        {
            dvbpsi_DetachPMT(state->pmthandles[i]);
            state->pmtpids[i]    = 0;
            state->pmthandles[i] = NULL;
            ServiceRefDec(state->services[i]);
            state->services[i]   = NULL;
        }
    }
    MultiplexRefDec(state->multiplex);
    ObjectRefDec(state);
    if (NewPMTCallbacksList)
    {
        ListFree(NewPMTCallbacksList, NULL);
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

/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/

static int PMTProcessorFilterPacket(PIDFilter_t *pidfilter, void *arg, uint16_t pid, TSPacket_t *packet)
{
    PMTProcessor_t *state = (PMTProcessor_t *)arg;
    int result = 0;
    if (state->multiplex)
    {
        int i;
        int count;
        Service_t **services;

        services = CacheServicesGet(&count);
        for (i = 0; i < count; i ++)
        {
            if (pid == services[i]->pmtPid)
            {
                result =  1;
                break;
            }
        }
        CacheServicesRelease();
    }
    return result;
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
        state->pmtpids[i] = services[i]->pmtPid;
        ServiceRefInc(services[i]);
        state->services[i] = services[i];
        state->pmthandles[i] = dvbpsi_AttachPMT(services[i]->id, PMTHandler, (void*)services[i]);
        state->payloadstartonly[i] = TRUE;
    }
    CacheServicesRelease();
    
    MultiplexRefDec(state->multiplex);
    state->multiplex = (Multiplex_t*)newmultiplex;
    MultiplexRefInc(state->multiplex);

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
    LogModule(LOG_DEBUG, PMTPROCESSOR, "PMTProcessor: TS Structure changed!\n");
    PMTProcessorMultiplexChanged(pidfilter, state, state->multiplex);
}

static void PMTHandler(void* arg, dvbpsi_pmt_t* newpmt)
{
    Service_t *service = (Service_t*)arg;
    ListIterator_t iterator;
    PIDList_t *pids;
    dvbpsi_pmt_es_t *esentry = newpmt->p_first_es;
    int count = 0;

    LogModule(LOG_DEBUG, PMTPROCESSOR, "PMT recieved, version %d on PID %d (old version %d)\n", newpmt->i_version, service->pmtPid, service->pmtVersion);

    while(esentry)
    {
        esentry = esentry->p_next;
        count ++;
    }
    LogModule(LOG_DEBUGV, PMTPROCESSOR, "%d PIDs in PMT\n", count);
    pids = PIDListNew(count);

    if (pids)
    {
        int i;
        esentry = newpmt->p_first_es;

        for (i = 0; i < count; i ++)
        {
            LogModule(LOG_DEBUGV, PMTPROCESSOR, "    0x%04x %d\n", esentry->i_pid, esentry->i_type);
            pids->pids[i].pid = esentry->i_pid;
            pids->pids[i].type = esentry->i_type;
            pids->pids[i].descriptors = esentry->p_first_descriptor;

            if ((esentry->i_type == 3) || (esentry->i_type == 4))
            {
                dvbpsi_descriptor_t *desc = esentry->p_first_descriptor;
                while(desc)
                {
                    LogModule(LOG_DEBUGV, PMTPROCESSOR, "        Descriptor %d\n", desc->i_tag);
                    if (desc->i_tag == 10) /* ISO 639 Language Descriptor */
                    {
                        dvbpsi_iso639_dr_t *iso639 = dvbpsi_DecodeISO639Dr(desc);
                        if (iso639)
                        {
                            pids->pids[i].subType = iso639->code[0].i_audio_type;
                        }
                    }
                    desc = desc->p_next;
                }
            }
            else
            {
                pids->pids[i].subType = 0;
            }
            esentry = esentry->p_next;
        }
        LogModule(LOG_DEBUGV,PMTPROCESSOR, "About to update cache\n");
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
    ObjectRefDec(newpmt);
}
