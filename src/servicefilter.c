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
 
servicefilter.c
 
Filter all packets for a service include the PMT, rewriting the PAT sent out in
the output to only include this service.
 
*/
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <assert.h>
#include <dvbpsi/dvbpsi.h>
#include <dvbpsi/psi.h>
#include <dvbpsi/pat.h>

#include "multiplexes.h"
#include "services.h"
#include "dvb.h"
#include "ts.h"
#include "main.h"
#include "cache.h"
#include "logging.h"

typedef struct ServiceFilter_t
{
    Multiplex_t  *multiplex;
    Service_t    *nextservice;
    Service_t    *service;
    int           rewritepat;
    unsigned int  version;
    unsigned char packetcounter;
    TSPacket_t    patpacket;
}ServiceFilter_t;

static int ServiceFilterFilterPacket(PIDFilter_t *pidfilter, void *arg, uint16_t pid, TSPacket_t *packet);
static TSPacket_t *ServiceFilterProcessPacket(PIDFilter_t *pidfilter, void *arg, TSPacket_t *packet);
static void ServiceFilterPATRewrite(TSPacket_t *packet, int tsid, int version, int serviceid, int pmtpid);

PIDFilter_t *ServiceFilterCreate(TSFilter_t *tsfilter, PacketOutput outputpacket,void *oparg)
{
    PIDFilter_t *result = NULL;
    ServiceFilter_t *state = calloc(1, sizeof(ServiceFilter_t));
    if (state)
    {
        result = PIDFilterSetup(tsfilter,
                    ServiceFilterFilterPacket, state,
                    ServiceFilterProcessPacket, state,
                    outputpacket, oparg);
        if (result == NULL)
        {
            free(state);
        }
    }
    return result;
}

void ServiceFilterDestroy(PIDFilter_t *filter)
{
    ServiceFilter_t *state = (ServiceFilter_t *)filter->pparg;
    assert(filter->filterpacket == ServiceFilterFilterPacket);
    PIDFilterFree(filter);
    free(state);
}

void ServiceFilterServiceSet(PIDFilter_t *filter, Service_t *service)
{
    ServiceFilter_t *state = (ServiceFilter_t *)filter->fparg;
    assert(filter->filterpacket == ServiceFilterFilterPacket);
    state->nextservice = service;
}

Service_t *ServiceFilterServiceGet(PIDFilter_t *filter)
{
    ServiceFilter_t *state = (ServiceFilter_t *)filter->fparg;
    assert(filter->filterpacket == ServiceFilterFilterPacket);
    return state->service;
}

static int ServiceFilterFilterPacket(PIDFilter_t *pidfilter, void *arg, uint16_t pid, TSPacket_t *packet)
{
    int i;
    ServiceFilter_t *state = (ServiceFilter_t *)arg;
    
    if (state->service != state->nextservice)
    {
        state->service = state->nextservice;
        state->multiplex = CurrentMultiplex;
        state->rewritepat = 1;
    }
    
    if (state->service)
    {
        int count;
        PID_t *pids;
        
        /* Handle PAT and PMT pids */
        if ((pid == 0) || (pid == state->service->pmtpid))
        {
            return 1;
        }
        
        pids = CachePIDsGet(state->service, &count);
        for (i = 0; i < count; i ++)
        {
            if (pid == pids[i].pid)
            {
                return 1;
            }
        }
    }
    return 0;
}

static TSPacket_t *ServiceFilterProcessPacket(PIDFilter_t *pidfilter, void *arg, TSPacket_t *packet)
{
    ServiceFilter_t *state = (ServiceFilter_t *)arg;
    unsigned short pid = TSPACKET_GETPID(*packet);
    /* If this is the PAT PID we need to rewrite it! */
    if (pid == 0)
    {
        if (state->rewritepat)
        {
            state->version ++;
            ServiceFilterPATRewrite(&state->patpacket, state->multiplex->tsid, state->version, state->service->id, state->service->pmtpid);
        }

        TSPACKET_SETCOUNT(state->patpacket, state->packetcounter ++);
        packet = &state->patpacket;
    }
    return packet;
}

static void ServiceFilterPATRewrite(TSPacket_t *packet, int tsid, int version, int serviceid, int pmtpid)
{
    dvbpsi_pat_t pat;
    dvbpsi_psi_section_t* section;
    uint8_t *data;
    int len,i;
    dvbpsi_InitPAT(&pat, tsid, version, 1);
    dvbpsi_PATAddProgram(&pat, serviceid, pmtpid);

    section = dvbpsi_GenPATSections(&pat, 1);

    // Fill in header
    packet->header[0] = 0x47;
    packet->header[1] = 0x40; // Payload unit start set
    packet->header[2] = 0x00;
    packet->header[3] = 0x10;
    packet->payload[0] = 0;   // Pointer field

    data = section->p_data;
    len = section->i_length + 3;

    if (len > (sizeof(TSPacket_t) - 5))
    {
        printlog(LOG_ERROR, "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"
							"! ERROR PAT too big to fit in 1 TS packet !\n"
							"!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    }

    for (i = 0; i < len; i ++)
    {
        packet->payload[1 + i] = data[i];
    }
    for (i = len + 1; i < 184; i ++)
    {
        packet->payload[i] = 0xff;
    }
    dvbpsi_DeletePSISections(section);
    dvbpsi_EmptyPAT(&pat);
}
