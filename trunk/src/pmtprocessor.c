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
#include <dvbpsi/dvbpsi.h>
#include <dvbpsi/descriptor.h>
#include <dvbpsi/dr.h>
#include <dvbpsi/pmt.h>

#include "multiplexes.h"
#include "services.h"
#include "dvb.h"
#include "ts.h"
#include "main.h"
#include "cache.h"
#include "logging.h"

#define MAX_HANDLES 20

typedef struct PMTProcessor_t
{
    Multiplex_t   *multiplex;
    Service_t     *services[MAX_HANDLES];
    unsigned short pmtpids[MAX_HANDLES];
    dvbpsi_handle  pmthandles[MAX_HANDLES];
}
PMTProcessor_t;

static int PMTProcessorFilterPacket(PIDFilter_t *pidfilter, void *arg, uint16_t pid, TSPacket_t *packet);
static TSPacket_t *PMTProcessorProcessPacket(PIDFilter_t *pidfilter, void *arg, TSPacket_t *packet);
static void PMTProcessorTSStructureChanged(PIDFilter_t *pidfilter, void *arg);
static void PMTHandler(void* arg, dvbpsi_pmt_t* newpmt);

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
        PIDFilterTSStructureChangeSet(result, PMTProcessorTSStructureChanged, state);
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

static TSPacket_t * PMTProcessorProcessPacket(PIDFilter_t *pidfilter, void *arg, TSPacket_t *packet)
{
    TSPacket_t *result = NULL;
    PMTProcessor_t *state = (PMTProcessor_t *)arg;
    unsigned short pid = TSPACKET_GETPID(*packet);
    int i;

    if (CurrentMultiplex == NULL)
    {
        return 0;
    }

    if (state->multiplex != CurrentMultiplex)
    {
        int count;
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
        for (i = 0; i < count; i ++)
        {
            state->pmtpids[i] = services[i]->pmtpid;
            state->services[i] = services[i];
            state->pmthandles[i] = dvbpsi_AttachPMT(services[i]->id, PMTHandler, (void*)services[i]);
        }
        state->multiplex = (Multiplex_t*)CurrentMultiplex;
    }

    for (i = 0; i < MAX_HANDLES; i ++)
    {
        if (state->pmthandles[i] && state->pmtpids[i] == pid)
        {
            dvbpsi_PushPacket(state->pmthandles[i], (uint8_t*)packet);
            break;
        }
    }

    return result;
}

static void PMTProcessorTSStructureChanged(PIDFilter_t *pidfilter, void *arg)
{
    PMTProcessor_t *state = (PMTProcessor_t *)arg;
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
            }
        }
    }

    
    for (i = 0; i < count; i ++)
    {
        int index;
        for (index = 0; index < MAX_HANDLES; index++)
        {
            if (state->pmthandles[i] == NULL)
            {
                break;
            }
        }
        if (index < MAX_HANDLES)
        {
            state->pmtpids[index] = services[i]->pmtpid;
            state->services[index] = services[i];
            state->pmthandles[index] = dvbpsi_AttachPMT(services[i]->id, PMTHandler, (void*)services[i]);
        }
    }
}

static void PMTHandler(void* arg, dvbpsi_pmt_t* newpmt)
{
    Service_t *service = (Service_t*)arg;
    printlog(LOG_DEBUG,"PMT recieved, version %d on PID %d (old version %d)\n", newpmt->i_version, service->pmtpid, service->pmtversion);
    if (service->pmtversion != newpmt->i_version)
    {
        // Version has changed update the pids
        PID_t *pids;
        dvbpsi_pmt_es_t *esentry = newpmt->p_first_es;
        int count = 1;

        while(esentry)
        {
            esentry = esentry->p_next;
            count ++;
        }
        printlog(LOG_DEBUGV,"%d PIDs in PMT\n", count);
        pids = calloc(count, sizeof(PID_t));

        if (pids)
        {
            int i;
            esentry = newpmt->p_first_es;

            // Store PCR PID
            pids[0].pid = newpmt->i_pcr_pid;
            pids[0].type = 0;
            pids[0].subtype = 0;

            for (i = 1; i < count; i ++)
            {
                printlog(LOG_DEBUGV, "0x%04x %d\n", esentry->i_pid, esentry->i_type);
                pids[i].pid = esentry->i_pid;
                pids[i].type = esentry->i_type;
#if 0
                if ((esentry->i_type == 3) || (esentry->i_type == 4))
                {
                    dvbpsi_descriptor_t *desc = esentry->p_first_descriptor;
                    while(desc)
                    {
                        printlog(LOG_DEBUGV,"Descriptor %d\n", desc->i_tag);
                        if (desc->i_tag == 10) /* ISO 639 Language Descriptor */
                        {
                            dvbpsi_iso639_dr_t *iso639 = (dvbpsi_iso639_dr_t*)desc->p_decoded;
                            pids[i].subtype = iso639->i_audio_type;
                        }
                        desc = desc->p_next;
                    }
                }
                else
#endif

                {
                    pids[i].subtype = 0;
                }
                esentry = esentry->p_next;
            }
            printlog(LOG_DEBUGV,"About to update cache\n");
            CacheUpdatePIDs(service, pids, count, newpmt->i_version);
        }
    }
    dvbpsi_DeletePMT(newpmt);
}
