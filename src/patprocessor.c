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

patprocessor.c

Process Program Association Tables and update the services information.
Rewrite the PAT so that only the current service appears in the PAT.

*/
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <dvbpsi/dvbpsi.h>
#include <dvbpsi/descriptor.h>
#include <dvbpsi/psi.h>
#include <dvbpsi/pat.h>

#include "multiplexes.h"
#include "services.h"
#include "dvb.h"
#include "ts.h"
#include "patprocessor.h"
#include "cache.h"
#include "main.h"

typedef struct PATProcessor_t
{
	Multiplex_t *multiplex;
	Service_t   *service;
	dvbpsi_handle pathandle;
	unsigned int version;
	unsigned char packetcounter;
	TSPacket_t patpacket;
}PATProcessor_t;

static void PATHandler(void* arg, dvbpsi_pat_t* newpat);
static void PATRewrite(TSPacket_t *packet, int tsid, int version, int serviceid, int pmtpid);

void *PATProcessorCreate()
{
	PATProcessor_t *result = calloc(1, sizeof(PATProcessor_t));
	return result;
}

void PATProcessorDestroy(void *arg)
{
	PATProcessor_t *state= (PATProcessor_t*)arg;
	if (state->multiplex)
	{
		dvbpsi_DetachPAT(state->pathandle);
	}
	free(state);
}

int PATProcessorProcessPacket(void *arg, TSPacket_t *packet)
{
	PATProcessor_t *state= (PATProcessor_t*)arg;
	
	if (state->multiplex != CurrentMultiplex)
	{
		if (state->multiplex)
		{
			dvbpsi_DetachPAT(state->pathandle);
		}
		state->multiplex = (Multiplex_t*)CurrentMultiplex;
		if (CurrentMultiplex)
		{
			state->pathandle = dvbpsi_AttachPAT(PATHandler, (void*)state);
		}
	}
	
	if (state->multiplex)
	{
		dvbpsi_PushPacket(state->pathandle, (uint8_t*)packet);
	}
	
	if (state->service != CurrentService)
	{
		state->service = (Service_t*)CurrentService;
		if (state->service)
		{
			state->version ++;
			PATRewrite(&state->patpacket, state->multiplex->tsid, state->version, state->service->id, state->service->pmtpid);
		}
	}
	
	if (state->service)
	{
		memcpy(packet, &state->patpacket, sizeof(TSPacket_t));
		TSPACKET_SETCOUNT(*packet, state->packetcounter ++);
		return 1;
	}
	return 0;
}

static void PATHandler(void* arg, dvbpsi_pat_t* newpat)
{
	PATProcessor_t *state = (PATProcessor_t*)arg;
    Multiplex_t *multiplex = state->multiplex;
	printlog(1,"PAT recieved, version %d (old version %d)\n", newpat->i_version, multiplex->patversion);
    if (multiplex->patversion != newpat->i_version)
    {
        // Version has changed update the services
        dvbpsi_pat_program_t *patentry = newpat->p_first_program;
        while(patentry)
        {
            Service_t *service = CacheServiceFindId(patentry->i_number);
            if (service && (service->pmtpid != patentry->i_pid))
            {
                CacheUpdateService(service, patentry->i_pid);
            }
            patentry = patentry->p_next;
        }
		CacheUpdateMultiplex(multiplex, newpat->i_version, newpat->i_ts_id);
    }
}

static void PATRewrite(TSPacket_t *packet, int tsid, int version, int serviceid, int pmtpid)
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
        printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
        printf("! ERROR PAT too big to fit in 1 TS packet !\n");
        printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
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
