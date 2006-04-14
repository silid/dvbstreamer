#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <linux/types.h>
typedef unsigned char   uint8_t;
typedef unsigned short  uint16_t;
typedef unsigned int    uint32_t;
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

#define MAX_HANDLES 20

typedef struct PMTProcessor_t
{
	Multiplex_t *multiplex;
	unsigned short pmtpids[MAX_HANDLES];
	dvbpsi_handle pmthandles[MAX_HANDLES];
}PMTProcessor_t;

static void PMTHandler(void* arg, dvbpsi_pmt_t* newpmt);

void *PMTProcessorCreate()
{
	PMTProcessor_t *result = calloc(1, sizeof(PMTProcessor_t));
	return result;
}

void PMTProcessorDestroy(void *arg)
{
	PMTProcessor_t *state = (PMTProcessor_t *)arg;
	int i;
	for (i = 0; i < MAX_HANDLES; i ++)
	{
		if (state->pmthandles[i])
		{
			dvbpsi_DetachPMT(state->pmthandles[i]);
			state->pmtpids[i]    = 0;
			state->pmthandles[i] = NULL;
		}
	}
	free(state);
}

int PMTProcessorFilterPacket(void *arg, TSPacket_t *packet)
{
	if (CurrentMultiplex)
	{
		int i;
		int count;
		Service_t **services;
		unsigned short pid = TSPACKET_GETPID(*packet);
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

int PMTProcessorProcessPacket(void *arg, TSPacket_t *packet)
{
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
			}
		}

		services = CacheServicesGet(&count);
		for (i = 0; i < count; i ++)
		{
			state->pmtpids[i] = services[i]->pmtpid;
			state->pmthandles[i] = dvbpsi_AttachPMT(services[i]->id, PMTHandler, (void*)services[i]);
		}
		state->multiplex = (Multiplex_t*)CurrentMultiplex;
	}
	
	for (i = 0; i < MAX_HANDLES; i ++)
	{
		if (state->pmtpids[i] == pid)
		{
			dvbpsi_PushPacket(state->pmthandles[i], (uint8_t*)packet);
			break;
		}
	}
	
	if (CurrentService == NULL)
	{
		return 0;
	}
	return pid == CurrentService->pmtpid;
}

static void PMTHandler(void* arg, dvbpsi_pmt_t* newpmt)
{
    Service_t *service = (Service_t*)arg;
	printlog(1,"PMT recieved, version %d on PID %d (old version %d)\n", newpmt->i_version, service->pmtpid, service->pmtversion);
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
		printlog(2,"%d PIDs in PMT\n", count);
		pids = calloc(count, sizeof(PID_t));
		
		if (pids)
		{
			int i;
			esentry = newpmt->p_first_es;
			
			// Store PCR PID
			pids[0].pid = newpmt->i_pcrpid;
			pids[0].type = 0;
			pids[0].subtype = 0;
			
			for (i = 1; i < count; i ++)
			{
				printlog(2, "0x%04x %d\n", esentry->i_pid, esentry->i_type);
				pids[i].pid = esentry->i_pid;
				pids[i].type = esentry->i_type;
				#if 0
				if ((esentry->i_type == 3) || (esentry->i_type == 4))
				{
					dvbpsi_descriptor_t *desc = esentry->p_first_descriptor;
					while(desc)
					{
						printlog(2,"Descriptor %d\n", desc->i_tag);
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
			printlog(2,"About to update cache\n");
			CacheUpdatePIDs(service, pids, count, newpmt->i_version);
		}
    }
}
