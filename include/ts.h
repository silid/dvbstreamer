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

ts.h

Transport stream processing and filter management.

*/
#ifndef _TS_H
#define _TS_H
#include <pthread.h>
#include "dvb.h"
#include "services.h"
#include "multiplexes.h"

/*------ Transport Stream Packet Structures and macros ----*/
#define TSPACKET_SIZE (188)

typedef struct TSPacket_t
{
	char header[4];
	char payload[TSPACKET_SIZE - 4];
}TSPacket_t;

#define TSPACKET_GETPID(packet) \
	(((((packet).header[1] & 0x1f) << 8) | ((packet).header[2] & 0xff)))

#define TSPACKET_SETPID(packet, pid) \
	do{ \
		(packet).header[1] = ((packet).header[1] & 0xe0) | ((pid >> 8) & 0x1f); \
		(packet).header[2] = pid & 0xff; \
	}while(0)

#define TSPACKET_GETCOUNT(packet) \
	((packet).header[3] & 0x0f)
	
#define TSPACKET_SETCOUNT(packet, count) \
	((packet).header[3] = ((packet).header[3] & 0xf0) | ((count) & 0x0f))


/*---- Filter function pointer type----*/
typedef int (*PacketProcessor)(void *userarg, TSPacket_t* packet);


/*---- PID Filter Structures ----*/
typedef struct PIDFilter_t
{
	struct TSFilter_t *tsfilter;
	volatile int enabled;
	
	volatile PacketProcessor filterpacket;
	volatile void *fparg;

	volatile PacketProcessor processpacket;
	volatile void *pparg;
	
	volatile PacketProcessor outputpacket;
	volatile void *oparg;
	
	volatile int packetsprocessed;
}PIDFilter_t;

/*---- Simple PID Filter structures ---*/
typedef struct PIDFilterSimpleFilter_t
{
	int pidcount;
	unsigned short pids[MAX_PIDS];
}PIDFilterSimpleFilter_t;

/*---- Transport Stream Filter structure ----*/
#define MAX_FILTERS 20	
#define MAX_PACKETS 20

typedef struct TSFilter_t
{
	int quit;
	TSPacket_t readBuffer[MAX_PACKETS];
	DVBAdapter_t *adapter;
	pthread_t thread;
	int enable;
	//pthread_mutex_t pidmutex;	
	
	struct {
		int allocated;
		struct PIDFilter_t filter;
	} pidfilters[MAX_FILTERS];
}TSFilter_t;

TSFilter_t* TSFilterCreate(DVBAdapter_t *adapter);
void TSFilterDestroy(TSFilter_t * tsfilter);
void TSFilterEnable(TSFilter_t * tsfilter, int enable);

PIDFilter_t* PIDFilterAllocate(TSFilter_t* tsfilter);
void PIDFilterFree(PIDFilter_t * pidfilter);

int PIDFilterSimpleFilter(void *arg, TSPacket_t *packet);

#endif
