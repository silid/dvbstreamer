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

#include <stdint.h>
#include <pthread.h>

#include "types.h"
#include "dvb.h"
#include "services.h"
#include "multiplexes.h"

/*------ Transport Stream Packet Structures and macros ----*/
#define TSPACKET_SIZE (188)

typedef struct TSPacket_t
{
    uint8_t header[4];
    uint8_t payload[TSPACKET_SIZE - 4];
}
TSPacket_t;

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

/* forward define of type to solve compile warnings */
typedef struct PIDFilter_t PIDFilter_t;

/*---- Filter function pointer type----*/
typedef void (*TSStructureChanged)(PIDFilter_t *pidfilter, void *userarg);
typedef int (*PacketFilter)(PIDFilter_t *pidfilter, void *userarg, uint16_t pid, TSPacket_t* packet);
typedef TSPacket_t* (*PacketProcessor)(PIDFilter_t *pidfilter, void *userarg, TSPacket_t* packet);
typedef void (*PacketOutput)(PIDFilter_t *pidfilter, void *userarg, TSPacket_t* packet);

/*---- PID Filter Structures ----*/
struct PIDFilter_t
{
    struct TSFilter_t *tsfilter;
    volatile bool enabled;

    TSStructureChanged tsstructurechanged;
    void *tscarg;

    PacketFilter filterpacket;
    void *fparg;

    PacketProcessor processpacket;
    void *pparg;

    PacketOutput outputpacket;
    void *oparg;

    /* Variables for statistics */
    volatile int packetsfiltered;
    volatile int packetsprocessed;
    volatile int packetsoutput;
};

/*---- Simple PID Filter structures ---*/
#define MAX_PIDS 20
typedef struct PIDFilterSimpleFilter_t
{
    int pidcount;
    uint16_t pids[MAX_PIDS];
}
PIDFilterSimpleFilter_t;

/*---- Transport Stream Filter structure ----*/
#define MAX_FILTERS 20
#define MAX_PACKETS 20

typedef struct TSFilter_t
{
    bool quit;
    TSPacket_t readbuffer[MAX_PACKETS];
    DVBAdapter_t *adapter;
    pthread_t thread;
    bool enabled;
    pthread_mutex_t mutex;
    bool tsstructurechanged;

    volatile int totalpackets;
	volatile int bitrate;

    struct
    {
        int allocated;
        struct PIDFilter_t filter;
    }
    pidfilters[MAX_FILTERS];
}
TSFilter_t;

TSFilter_t* TSFilterCreate(DVBAdapter_t *adapter);
void TSFilterDestroy(TSFilter_t * tsfilter);
void TSFilterEnable(TSFilter_t * tsfilter, bool enable);
void TSFilterZeroStats(TSFilter_t *tsfilter);
#define TSFilterLock(tsfilter)   pthread_mutex_lock(&(tsfilter)->mutex)
#define TSFilterUnLock(tsfilter) pthread_mutex_unlock(&(tsfilter)->mutex)

PIDFilter_t* PIDFilterAllocate(TSFilter_t* tsfilter);
void PIDFilterFree(PIDFilter_t * pidfilter);
PIDFilter_t *PIDFilterSetup(TSFilter_t *tsfilter,
                            PacketFilter filterpacket,  void *fparg,
                            PacketProcessor processpacket, void *pparg,
                            PacketOutput outputpacket,  void *oparg);

#define PIDFilterFilterPacketSet(_pidfilter, _callback, _arg) \
    do{ (_pidfilter)->fparg = _arg; (_pidfilter)->filterpacket = _callback; } while(0)

#define PIDFilterProcessPacketSet(_pidfilter, _callback, _arg) \
    do{ (_pidfilter)->pparg = _arg; (_pidfilter)->processpacket = _callback; } while(0)

#define PIDFilterOutputPacketSet(_pidfilter, _callback, _arg) \
    do{ (_pidfilter)->oparg = _arg; (_pidfilter)->outputpacket = _callback; } while(0)

#define PIDFilterTSStructureChangeSet(_pidfilter, _callback, _arg) \
    do{ (_pidfilter)->tscarg = _arg; (_pidfilter)->tsstructurechanged = _callback; } while(0)

int PIDFilterSimpleFilter(PIDFilter_t *pidfilter, void *arg, uint16_t pid, TSPacket_t *packet);

#endif