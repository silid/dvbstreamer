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

/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/
typedef struct ServiceFilter_t
{
    Multiplex_t  *currentMultiplex;
    Service_t    *service;
    Service_t    *nextService;
    bool          serviceChanged;
    pthread_mutex_t serviceChangeMutex;
    
    bool          rewritePAT;
    uint16_t      patVersion;
    uint8_t       patPacketCounter;
    TSPacket_t    patPacket;

    bool          avsOnly;
    bool          rewritePMT;
    uint16_t      serviceVersion;
    uint16_t      pmtVersion;
    uint8_t       pmtPacketCounter;
    uint16_t      videoPID;
    uint16_t      audioPID;
    uint16_t      subPID;
    TSPacket_t    pmtPacket;
}ServiceFilter_t;

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static int ServiceFilterFilterPacket(PIDFilter_t *pidfilter, void *arg, uint16_t pid, TSPacket_t *packet);
static TSPacket_t *ServiceFilterProcessPacket(PIDFilter_t *pidfilter, void *arg, TSPacket_t *packet);
static void ServiceFilterMultiplexChanged(PIDFilter_t *pidfilter, void *arg, Multiplex_t *newmultiplex);
static void ServiceFilterPATRewrite(ServiceFilter_t *state);
static void ServiceFilterPMTRewrite(ServiceFilter_t *state);
static void ServiceFilterInitPacket(TSPacket_t *packet, dvbpsi_psi_section_t* section, char *sectionname);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static char SERVICEFILTER[] = "ServiceFilter";

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
PIDFilter_t *ServiceFilterCreate(TSFilter_t *tsfilter, PacketOutput outputpacket,void *oparg)
{
    PIDFilter_t *result = NULL;
    ServiceFilter_t *state;
    ObjectRegisterType(ServiceFilter_t);
    state = ObjectCreateType(ServiceFilter_t);
    if (state)
    {
        result = PIDFilterSetup(tsfilter,
                    ServiceFilterFilterPacket, state,
                    ServiceFilterProcessPacket, state,
                    outputpacket, oparg);
        if (result == NULL)
        {
            ObjectRefDec(state);
        }
        PIDFilterMultiplexChangeSet(result, ServiceFilterMultiplexChanged, state);
        pthread_mutex_init(&state->serviceChangeMutex, NULL);
    }
    return result;
}

void ServiceFilterDestroy(PIDFilter_t *filter)
{
    ServiceFilter_t *state = (ServiceFilter_t *)filter->ppArg;
    assert(filter->filterPacket == ServiceFilterFilterPacket);
    PIDFilterFree(filter);
    if (state->serviceChanged)
    {
        ServiceRefDec(state->nextService);
    }
    MultiplexRefDec(state->currentMultiplex);
    ServiceRefDec(state->service);
    pthread_mutex_destroy(&state->serviceChangeMutex);
    ObjectRefDec(state);
}

void ServiceFilterServiceSet(PIDFilter_t *filter, Service_t *service)
{
    ServiceFilter_t *state = (ServiceFilter_t *)filter->fpArg;
    assert(filter->filterPacket == ServiceFilterFilterPacket);
    
    ServiceRefInc(service);

    pthread_mutex_lock(&state->serviceChangeMutex);

    /* Service already waiting so unref it */
    if (state->serviceChanged)
    {
        ServiceRefDec(state->nextService);
    }
    state->nextService = service;
    state->serviceChanged = TRUE;

    pthread_mutex_unlock(&state->serviceChangeMutex);
}

Service_t *ServiceFilterServiceGet(PIDFilter_t *filter)
{
    ServiceFilter_t *state = (ServiceFilter_t *)filter->fpArg;
    assert(filter->filterPacket == ServiceFilterFilterPacket);
    return state->service;
}

void ServiceFilterAVSOnlySet(PIDFilter_t *filter, bool enable)
{
    ServiceFilter_t *state = (ServiceFilter_t *)filter->fpArg;
    assert(filter->filterPacket == ServiceFilterFilterPacket);
    state->rewritePMT = enable;
    state->avsOnly = enable;
}

bool ServiceFilterAVSOnlyGet(PIDFilter_t *filter)
{
    ServiceFilter_t *state = (ServiceFilter_t *)filter->fpArg;
    assert(filter->filterPacket == ServiceFilterFilterPacket);
    return state->avsOnly;
}

/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/
static int ServiceFilterFilterPacket(PIDFilter_t *pidfilter, void *arg, uint16_t pid, TSPacket_t *packet)
{
    int result = 0;
    int i;
    ServiceFilter_t *state = (ServiceFilter_t *)arg;

    if (state->serviceChanged)
    {
        pthread_mutex_lock(&state->serviceChangeMutex);
        ServiceRefDec(state->service);
        
        state->service = state->nextService;

        state->serviceChanged = FALSE;
        state->rewritePAT = TRUE;
        state->rewritePMT = TRUE;

        pthread_mutex_unlock(&state->serviceChangeMutex);
    }

    if (state->service)
    {
        PIDList_t *pids;

        /* Is this service on the current multiplex ? */
        if ((!state->currentMultiplex) || (state->service->multiplexUID != state->currentMultiplex->uid))
        {
            return 0;
        }
        
        /* Handle PAT and PMT pids */
        if ((pid == 0) || (pid == state->service->pmtPid) || (pid == state->service->pcrPid))
        {
            return 1;
        }

        if (state->avsOnly)
        {
            if ((state->videoPID == pid) || (state->audioPID == pid) || (state->subPID == pid))
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
                        result = 1;
                        break;
                    }
                }
            }
            CachePIDsRelease();
        }
    }
    return result;
}

static TSPacket_t *ServiceFilterProcessPacket(PIDFilter_t *pidfilter, void *arg, TSPacket_t *packet)
{
    ServiceFilter_t *state = (ServiceFilter_t *)arg;
    unsigned short pid = TSPACKET_GETPID(*packet);
    /* If this is the PAT PID we need to rewrite it! */
    if (pid == 0)
    {
        if (state->rewritePAT)
        {
            state->patVersion ++;
            ServiceFilterPATRewrite(state);
            state->rewritePAT = FALSE;

        }

        TSPACKET_SETCOUNT(state->patPacket, state->patPacketCounter ++);
        packet = &state->patPacket;
    }

    if (state->avsOnly && (pid == state->service->pmtPid))
    {
        if (state->rewritePMT || (state->serviceVersion != state->service->pmtVersion))
        {
            state->pmtVersion ++;
            ServiceFilterPMTRewrite(state);
            state->rewritePMT = FALSE;
            state->serviceVersion = state->service->pmtVersion;
        }

        TSPACKET_SETCOUNT(state->pmtPacket, state->pmtPacketCounter ++);
        packet = &state->pmtPacket;
    }

    return packet;
}

static void ServiceFilterMultiplexChanged(PIDFilter_t *pidfilter, void *arg, Multiplex_t *newmultiplex)
{
    ServiceFilter_t *state = (ServiceFilter_t *)arg;

    MultiplexRefDec(state->currentMultiplex);
    state->currentMultiplex = newmultiplex;
    MultiplexRefInc(state->currentMultiplex);
}

static void ServiceFilterPATRewrite(ServiceFilter_t *state)
{
    dvbpsi_pat_t pat;
    dvbpsi_psi_section_t* section;

    MultiplexRefInc(state->currentMultiplex);
    dvbpsi_InitPAT(&pat, state->currentMultiplex->tsId, state->patVersion, 1);
    MultiplexRefDec(state->currentMultiplex);
    
    dvbpsi_PATAddProgram(&pat, state->service->id, state->service->pmtPid);

    section = dvbpsi_GenPATSections(&pat, 1);
    ServiceFilterInitPacket(&state->patPacket, section, "PAT");

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

    dvbpsi_InitPMT(&pmt, state->service->id, state->pmtVersion, 1, state->service->pcrPid);

    state->videoPID = 0xffff;
    state->audioPID = 0xffff;
    state->subPID = 0xffff;
    LogModule(LOG_DEBUG, SERVICEFILTER, "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    LogModule(LOG_DEBUG, SERVICEFILTER, "Rewriting PMT on PID %x\n", state->service->pmtPid);
    pids = CachePIDsGet(state->service);
    for (i = 0; (pids != NULL) && (i < pids->count) && (!vfound || !afound || !sfound); i ++)
    {
        LogModule(LOG_DEBUG, SERVICEFILTER, "\tpid = %x type =%d subtype = %d\n", pids->pids[i].pid, pids->pids[i].type, pids->pids[i].subType);
        if (!vfound && ((pids->pids[i].type == 1) || (pids->pids[i].type == 2)))
        {
            vfound = TRUE;
            state->videoPID = pids->pids[i].pid;
            es = dvbpsi_PMTAddES(&pmt, pids->pids[i].type,  pids->pids[i].pid);
        }

        if (!afound && ((pids->pids[i].type == 3) || (pids->pids[i].type == 4)))
        {
            afound = TRUE;
            state->audioPID = pids->pids[i].pid;
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
                    state->subPID = pids->pids[i].pid;
                    es = dvbpsi_PMTAddES(&pmt, pids->pids[i].type,  pids->pids[i].pid);
                    dvbpsi_PMTESAddDescriptor(es, desc->i_tag, desc->i_length,  desc->p_data);
                    break;
                }
                /* AC3 */
                if (!afound && (desc->i_tag == 0x6a))
                {

                    afound = TRUE;
                    state->audioPID = pids->pids[i].pid;
                    es = dvbpsi_PMTAddES(&pmt, pids->pids[i].type,  pids->pids[i].pid);
                    dvbpsi_PMTESAddDescriptor(es, desc->i_tag, desc->i_length,  desc->p_data);
                    break;
                }
                desc = desc->p_next;
            }
        }
    }
    CachePIDsRelease();
    LogModule(LOG_DEBUG, SERVICEFILTER, "videopid = %x audiopid = %x subpid = %x\n", state->videoPID,state->audioPID,state->subPID);

    section = dvbpsi_GenPMTSections(&pmt);
    ServiceFilterInitPacket(&state->pmtPacket, section, "PMT");
    TSPACKET_SETPID(state->pmtPacket, state->service->pmtPid);
    LogModule(LOG_DEBUG, SERVICEFILTER, "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
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
        LogModule(LOG_ERROR, SERVICEFILTER, "!!! ERROR %s section too big to fit in 1 TS packet !!!\n" );
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

