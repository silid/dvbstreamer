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
#include "pids.h"
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

    bool          rewritepat;
    uint16_t      patversion;
    uint8_t       patpacketcounter;
    TSPacket_t    patpacket;

    bool          avsonly;
    bool          rewritepmt;
    uint16_t      serviceversion;
    uint16_t      pmtversion;
    uint8_t       pmtpacketcounter;
    uint16_t      videopid;
    uint16_t      audiopid;
    uint16_t      subpid;
    TSPacket_t    pmtpacket;
}ServiceFilter_t;

static int ServiceFilterFilterPacket(PIDFilter_t *pidfilter, void *arg, uint16_t pid, TSPacket_t *packet);
static TSPacket_t *ServiceFilterProcessPacket(PIDFilter_t *pidfilter, void *arg, TSPacket_t *packet);
static void ServiceFilterPATRewrite(ServiceFilter_t *state);
static void ServiceFilterPMTRewrite(ServiceFilter_t *state);
static void ServiceFilterInitPacket(TSPacket_t *packet, dvbpsi_psi_section_t* section, char *sectionname);

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
    if (service)
    {
        ServiceRefInc(service);
    }
    state->nextservice = service;
}

Service_t *ServiceFilterServiceGet(PIDFilter_t *filter)
{
    ServiceFilter_t *state = (ServiceFilter_t *)filter->fparg;
    assert(filter->filterpacket == ServiceFilterFilterPacket);
    return state->service;
}

void ServiceFilterAVSOnlySet(PIDFilter_t *filter, bool enable)
{
    ServiceFilter_t *state = (ServiceFilter_t *)filter->fparg;
    assert(filter->filterpacket == ServiceFilterFilterPacket);
    state->rewritepmt = enable;
    state->avsonly = enable;
}

bool ServiceFilterAVSOnlyGet(PIDFilter_t *filter)
{
    ServiceFilter_t *state = (ServiceFilter_t *)filter->fparg;
    assert(filter->filterpacket == ServiceFilterFilterPacket);
    return state->avsonly;
}

static int ServiceFilterFilterPacket(PIDFilter_t *pidfilter, void *arg, uint16_t pid, TSPacket_t *packet)
{
    int i;
    ServiceFilter_t *state = (ServiceFilter_t *)arg;

    if (state->service != state->nextservice)
    {
        if (state->service)
        {
            ServiceRefDec(state->service);
        }
        state->service = state->nextservice;
        state->multiplex = (Multiplex_t *)CurrentMultiplex;
        state->rewritepat = TRUE;
        state->rewritepmt = TRUE;
    }

    if (state->service)
    {
        int count;
        PIDList_t *pids;

        /* Handle PAT and PMT pids */
        if ((pid == 0) || (pid == state->service->pmtpid) || (pid == state->service->pcrpid))
        {
            return 1;
        }

        if (state->avsonly)
        {
            if ((state->videopid == pid) || (state->audiopid == pid) || (state->subpid == pid))
            {
                return 1;
            }
        }
        else
        {
            pids = CachePIDsGet(state->service);
            if (pids)
            {
                for (i = 0; i < pids->count; i ++)
                {
                    if (pid == pids->pids[i].pid)
                    {
                        return 1;
                    }
                }
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
            state->patversion ++;
            ServiceFilterPATRewrite(state);
            state->rewritepat = FALSE;

        }

        TSPACKET_SETCOUNT(state->patpacket, state->patpacketcounter ++);
        packet = &state->patpacket;
    }

    if (state->avsonly && (pid == state->service->pmtpid))
    {
        if (state->rewritepmt || (state->serviceversion != state->service->pmtversion))
        {
            state->pmtversion ++;
            ServiceFilterPMTRewrite(state);
            state->rewritepmt = FALSE;
            state->serviceversion = state->service->pmtversion;
        }

        TSPACKET_SETCOUNT(state->pmtpacket, state->pmtpacketcounter ++);
        packet = &state->pmtpacket;
    }

    return packet;
}

static void ServiceFilterPATRewrite(ServiceFilter_t *state)
{
    dvbpsi_pat_t pat;
    dvbpsi_psi_section_t* section;

    dvbpsi_InitPAT(&pat, state->multiplex->tsid, state->patversion, 1);
    dvbpsi_PATAddProgram(&pat, state->service->id, state->service->pmtpid);

    section = dvbpsi_GenPATSections(&pat, 1);
    ServiceFilterInitPacket(&state->patpacket, section, "PAT");

    dvbpsi_DeletePSISections(section);
    dvbpsi_EmptyPAT(&pat);
}

static void ServiceFilterPMTRewrite(ServiceFilter_t *state)
{
    int i;
    PIDList_t *pids;
    bool vfound = FALSE;
    bool afound = FALSE;
    bool sfound = FALSE;
    dvbpsi_pmt_t pmt;
    dvbpsi_pmt_es_t *es;
    dvbpsi_psi_section_t* section;

    dvbpsi_InitPMT(&pmt, state->service->id, state->pmtversion, 1, state->service->pcrpid);

    state->videopid = 0xffff;
    state->audiopid = 0xffff;
    state->subpid = 0xffff;
    printlog(LOG_DEBUG, "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    printlog(LOG_DEBUG, "Rewriting PMT on PID %x\n", state->service->pmtpid);
    pids = CachePIDsGet(state->service);
    for (i = 0; (pids != NULL) && (i < pids->count) && (!vfound || !afound || !sfound); i ++)
    {
        printlog(LOG_DEBUG, "\tpid = %x type =%d subtype = %d\n", pids->pids[i].pid, pids->pids[i].type, pids->pids[i].subtype);
        if (!vfound && ((pids->pids[i].type == 1) || (pids->pids[i].type == 2)))
        {
            vfound = TRUE;
            state->videopid = pids->pids[i].pid;
            es = dvbpsi_PMTAddES(&pmt, pids->pids[i].type,  pids->pids[i].pid);
        }

        if (!afound && ((pids->pids[i].type == 3) || (pids->pids[i].type == 4)))
        {
            afound = TRUE;
            state->audiopid = pids->pids[i].pid;
            es = dvbpsi_PMTAddES(&pmt, pids->pids[i].type,  pids->pids[i].pid);
        }

        if (pids->pids[i].type == 6)
        {
            dvbpsi_descriptor_t *desc = pids->pids[i].descriptors;
            while(desc)
            {
                /* DVB Subtitles */
                if (!sfound  && (desc->i_tag == 0x59))
                {
                    sfound = TRUE;
                    state->subpid = pids->pids[i].pid;
                    es = dvbpsi_PMTAddES(&pmt, pids->pids[i].type,  pids->pids[i].pid);
                    dvbpsi_PMTESAddDescriptor(es, desc->i_tag, desc->i_length,  desc->p_data);
                    break;
                }
                /* AC3 */
                if (!afound && (desc->i_tag == 0x6a))
                {

                    afound = TRUE;
                    state->audiopid = pids->pids[i].pid;
                    es = dvbpsi_PMTAddES(&pmt, pids->pids[i].type,  pids->pids[i].pid);
                    dvbpsi_PMTESAddDescriptor(es, desc->i_tag, desc->i_length,  desc->p_data);
                    break;
                }
                desc = desc->p_next;
            }
        }
    }
    printlog(LOG_DEBUG, "videopid = %x audiopid = %x subpid = %x\n", state->videopid,state->audiopid,state->subpid);

    section = dvbpsi_GenPMTSections(&pmt);
    ServiceFilterInitPacket(&state->pmtpacket, section, "PMT");
    TSPACKET_SETPID(state->pmtpacket, state->service->pmtpid);
    printlog(LOG_DEBUG, "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    dvbpsi_DeletePSISections(section);
    dvbpsi_EmptyPMT(&pmt);
}

static void ServiceFilterInitPacket(TSPacket_t *packet, dvbpsi_psi_section_t* section, char *sectionname)
{
    uint8_t *data;
    int len,i;
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
        printlog(LOG_ERROR, "!!! ERROR %s section too big to fit in 1 TS packet !!!\n" );
    }

    for (i = 0; i < len; i ++)
    {
        packet->payload[1 + i] = data[i];
    }
    for (i = len + 1; i < 184; i ++)
    {
        packet->payload[i] = 0xff;
    }
}

