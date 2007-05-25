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
#include "list.h"

/*------ Transport Stream Packet Structures and macros ----*/
/**
 * @defgroup TSPacket Transport Stream Packet structure and macros
 * @{
 */

/**
 * Constant for the size of a transport packet with out hamming code.
 */
#define TSPACKET_SIZE (188)

/**
 * Structure representing an MPEG2 Transport Stream packet
 * with out hamming codes.
 */
typedef struct TSPacket_t
{
    uint8_t header[4];                  /**< Packet Header fields */
    uint8_t payload[TSPACKET_SIZE - 4]; /**< Data contained in the packet */
}
TSPacket_t;

/**
 * Retrieves the PID of packet from the packet header
 * @param packet The packet to extract the PID from.
 * @return The PID of the packet as a 16bit integer.
 */
#define TSPACKET_GETPID(packet) \
	(((((packet).header[1] & 0x1f) << 8) | ((packet).header[2] & 0xff)))

/**
 * Sets the PID of the packet in the packet header.
 * @param packet The packet to update.
 * @param pid    The new PID to set.
 */
#define TSPACKET_SETPID(packet, pid) \
	do{ \
		(packet).header[1] = ((packet).header[1] & 0xe0) | ((pid >> 8) & 0x1f); \
		(packet).header[2] = pid & 0xff; \
	}while(0)
/**
 * Retrieves the packet sequence count.
 * @param packet The packet to extract the count from.
 * @return The packet sequence count as a 4 bit integer.
 */
#define TSPACKET_GETCOUNT(packet) \
	((packet).header[3] & 0x0f)

/**
 * Sets the packet sequence count.
 * @param packet The packet to update.
 * @param count  The new sequence count to set.
 */
#define TSPACKET_SETCOUNT(packet, count) \
	((packet).header[3] = ((packet).header[3] & 0xf0) | ((count) & 0x0f))

/**
 * Boolean test to determine whether this packet is the start of a payload.
 * @param packet The packet to check.
 */
#define TSPACKET_ISPAYLOADUNITSTART(packet) \
    (((packet).header[1] & 0x40) == 0x40)

/**@}*/

/**
 * @defgroup PIDFilter PID Filter functions and datatypes
 * @{
 */

struct PIDFilter_s;

/*---- Filter function pointer type----*/
/**
 * Callback used to signal that the transport stream has changed to a new multiplex.
 *
 * @param pidfilter The PID Filter this callback belongs to.
 * @param userarg   A user defined argument.
 */
typedef void (*MultiplexChanged)(struct PIDFilter_s *pidfilter, void *userarg, Multiplex_t *multiplex);

/**
 * Callback used to signal that a change has occured to the underlying structure
 * of the transport stream.
 * @param pidfilter The PID Filter this callback belongs to.
 * @param userarg   A user defined argument.
 */
typedef void (*TSStructureChanged)(struct PIDFilter_s *pidfilter, void *userarg);

/**
 * Callback used to determine if a packet should be passed to the packet processor
 * or output callbacks.
 * @param pidFilter The PID Filter this callback belongs to.
 * @param userarg   A user defined argument.
 * @param pid       PID of the packet in question.
 * @param packet    The packet in question.
 * @return 0 if the packet should not be processed/output, any other value if the
 * packet should be passed on to the Packet processor callback if one exists, or
 * to the output callback.
 */
typedef int (*PacketFilter)(struct PIDFilter_s *pidfilter, void *userarg, uint16_t pid, TSPacket_t* packet);

/**
 * Callback used to process a packet, this is intended to be a function which
 * needs more time to process a packet than just a simple filter.
 * @param pidFilter The PID Filter this callback belongs to.
 * @param userarg   A user defined argument.
 * @param packet    The packet in question.
 * @return The packet to pass to the output callback, this can be null if no
 * packet should be output. Returning the packet allows the processor to either
 * return the original pointer or to insert a packet of its own creation.
 */
typedef TSPacket_t* (*PacketProcessor)(struct PIDFilter_s *pidfilter, void *userarg, TSPacket_t* packet);

/**
 * Callback used to send a packet to an output destination.
 * @param pidFilter The PID Filter this callback belongs to.
 * @param userarg   A user defined argument.
 * @param packet    The packet to output.
 */
typedef void (*PacketOutput)(struct PIDFilter_s *pidfilter, void *userarg, TSPacket_t* packet);

/*---- PID Filter Structures ----*/
/**
 * Structure representing a PID Filter that belongs to a TS Filter.
 */
typedef struct PIDFilter_s
{
    char *name;                            /**< Name of this instance */
    struct TSFilter_t *tsFilter;           /**< TS Filter instance this filter belongs to. */
    volatile bool enabled;                 /**< Boolean indicating whether this filter is enabled and should process packets */

    MultiplexChanged multiplexChanged;     /**< Callback to call when the Multiplex is changed. */
    void *mcArg;                           /**< User defined argument to pass to the multiplexChanged callback */

    TSStructureChanged tsStructureChanged; /**< Callback to call when the underlying TS structure changes */
    void *tscArg;                          /**< User defined argument to pass to the tsStructureChanged callback */

    PacketFilter filterPacket;             /**< Callback to call when a new packet arrives */
    void *fpArg;                           /**<User defined argument to pass to the filterPacket callback */

    PacketProcessor processPacket;         /**< Callback to call if filterPacket returns non-zero */
    void *ppArg;                           /**<User defined argument to pass to the processPacket callback */

    PacketOutput outputPacket;             /**< Callback to call when a packet has passed filterPacket and processPacket callbacks tests */
    void *opArg;                           /**<User defined argument to pass to the outputPacket callback */

    /* Variables for statistics */
    volatile unsigned long long packetsFiltered;          /**< Number of packets that filterPacket has returned non-zero for. */
    volatile unsigned long long packetsProcessed;         /**< Number of packets that processPacket has returned non-NULL for. */
    volatile unsigned long long packetsOutput;            /**< Number of packets sent to the output callback */
}PIDFilter_t;

/**@}*/

/**
 * @defgroup SimplePIDFilter Simple PID Filter packet filter structure and functions
 * @{
 */

/*---- Simple PID Filter structures ---*/
/**
 * Maximum number of PIDs that can be added to a Simple PID Filter
 */
#define MAX_PIDS 20

/**
 * Structure describing a simple PID filter that just checks the PID of the
 * packet matches one contained in the pids array.
 */
typedef struct PIDFilterSimpleFilter_t
{
    int pidcount;            /**< Number of PIDs in the pids array. */
    uint16_t pids[MAX_PIDS]; /**< Array of PIDs to accept */
}
PIDFilterSimpleFilter_t;

/**@}*/

/*---- Transport Stream Filter structure ----*/
/**
 * @defgroup TSFilter Transport Stream filter structure and functions
 * @{
 */
/**
 * Maximum number of filters supported by a TSFilter_t instance.
 */
#define MAX_FILTERS 20

/**
 * Maximum number of packets to read from the DVB adapter in one go,
 */
#define MAX_PACKETS 20

/**
 * Structure describing a Transport Stream Filter instance.
 */
typedef struct TSFilter_t
{
    bool quit;                          /**< Whether the filters thread should finish. */
    TSPacket_t readBuffer[MAX_PACKETS]; /**< Buffer used to read packets into from the DVB Adapter */
    DVBAdapter_t *adapter;              /**< DVBAdapter packets should be read from */
    pthread_t thread;                   /**< Thread used to read and process TS packets */
    bool enabled;                       /**< Whether packets should be read/processed */
    pthread_mutex_t mutex;              /**< Mutex used to protect access to this structure */
    bool multiplexChanged;              /**< Whether the multiplex has been changed. */
    Multiplex_t *multiplex;             /**< The multiplex the transport stream is coming from. */
    bool tsStructureChanged;            /**< Whether the underlying TS structure has changed. */

    volatile unsigned long long totalPackets; /**< Total number of packets processed by this instance. */
    volatile unsigned long long bitrate;      /**< Approximate bit rate of the transport stream being processed. */

    List_t *pidFilters;
}
TSFilter_t;

/**
 * Create a new TSFilter_t instance that is processing packets from the specified
 * DVBAdapter instance.
 * @param adapter The DVBAdapter to read packets from.
 * @return A new TSFilter_t instance or NULL if one could not be created.
 */
TSFilter_t* TSFilterCreate(DVBAdapter_t *adapter);

/**
 * Stop and Destroy the specified TSFilter_t instance.
 * @param tsfilter The TSFilter_t instance to destroy.
 */
void TSFilterDestroy(TSFilter_t * tsfilter);

/**
 * Enable/Disable processing of packets by the specified TSFilter_t instance.
 * @param tsfilter The instance to enable or disable packet processing on.
 * @param enable   TRUE to enable processing packets, FALSE to disable processing.
 */
void TSFilterEnable(TSFilter_t * tsfilter, bool enable);

/**
 * Zero total packets and bit rate fields as well as all packet statistics fields
 * in all PIDFilters.
 * @param tsfilter TSFilter_t instance to zero the stats of.
 */
void TSFilterZeroStats(TSFilter_t *tsfilter);

/**
 * Informs all PID filters that the multiplex has changed to newmultiplex.
 * @param tsfilter TSFilter_t instance to inform.
 * @param newmultiplex The new multiplex that the DVB adapter is now tuned to.
 */
void TSFilterMultiplexChanged(TSFilter_t *tsfilter, Multiplex_t *newmultiplex);

/**
 * Lock access to the TSFilter_t structure to this thread.
 * @param tsfilter The instance to lock access to.
 */
#define TSFilterLock(tsfilter)   pthread_mutex_lock(&(tsfilter)->mutex)

/**
 * Unlock access to the TSFilter_t structure.
 * @param tsfilter The instance to unlock access to.
 */
#define TSFilterUnLock(tsfilter) pthread_mutex_unlock(&(tsfilter)->mutex)


/**@}*/

/**
 * @addtogroup PIDFilter
 * @{
 */
/**
 * Allocate a PID filter for the specified TSFilter_t instance.
 * @param tsfilter The TSFilter_t instance to allocate the PID filter for.
 * @return A PIDFilter pointer or NULL if one could not be allocated.
 */
PIDFilter_t* PIDFilterAllocate(TSFilter_t* tsfilter);
/**
 * Free a PIDFilter_t instance.
 * @param pidfilter The instance to release.
 */
void PIDFilterFree(PIDFilter_t * pidfilter);

/**
 * Creates and initialise a new PIDFIlter_t instance.
 * @param tsfilter      The TSFilter_t instance to allocate the PID filter for.
 * @param filterpacket  The callback to use to filter packets.
 * @param fparg         The user argument to pass to the filter packet callback.
 * @param processpacket The callback to use to proces packets.
 * @param pparg         The user argument ot pass to the process packet callback.
 * @param outputpacket  The callback to use to output packets.
 * @param oparg         The user argument to pass to the output packet callback.
 * @return A PIDFilter pointer or NULL if one could not be allocated.
 */
PIDFilter_t *PIDFilterSetup(TSFilter_t *tsfilter,
                            PacketFilter filterpacket,     void *fparg,
                            PacketProcessor processpacket, void *pparg,
                            PacketOutput outputpacket,     void *oparg);

/**
 * Sets the filterpacket callback and user argument.
 * @param _pidfilter The PIDFilter_t instance to set.
 * @param _callback  The function to set as the filterpacket callback.
 * @param _arg       The user argument to pass to the callback.
 */
#define PIDFilterFilterPacketSet(_pidfilter, _callback, _arg) \
    do{ (_pidfilter)->fpArg = _arg; (_pidfilter)->filterPacket = _callback; } while(0)
/**
 * Sets the processpacket callback and user argument.
 * @param _pidfilter The PIDFilter_t instance to set.
 * @param _callback  The function to set as the processpacket callback.
 * @param _arg       The user argument to pass to the callback.
 */
#define PIDFilterProcessPacketSet(_pidfilter, _callback, _arg) \
    do{ (_pidfilter)->ppArg = _arg; (_pidfilter)->processPacket = _callback; } while(0)
/**
 * Sets the outputpacket callback and user argument.
 * @param _pidfilter The PIDFilter_t instance to set.
 * @param _callback  The function to set as the outputpacket callback.
 * @param _arg       The user argument to pass to the callback.
 */
#define PIDFilterOutputPacketSet(_pidfilter, _callback, _arg) \
    do{ (_pidfilter)->opArg = _arg; (_pidfilter)->outputPacket = _callback; } while(0)
/**
 * Sets the tsstructurechanged callback and user argument.
 * @param _pidfilter The PIDFilter_t instance to set.
 * @param _callback  The function to set as the tsstructurechanged callback.
 * @param _arg       The user argument to pass to the callback.
 */
#define PIDFilterTSStructureChangeSet(_pidfilter, _callback, _arg) \
    do{ (_pidfilter)->tscArg = _arg; (_pidfilter)->tsStructureChanged = _callback; } while(0)
/**
 * Sets the multiplexchanged callback and user argument.
 * @param _pidfilter The PIDFilter_t instance to set.
 * @param _callback  The function to set as the multiplexchanged callback.
 * @param _arg       The user argument to pass to the callback.
 */
#define PIDFilterMultiplexChangeSet(_pidfilter, _callback, _arg) \
    do{ (_pidfilter)->mcArg = _arg; (_pidfilter)->multiplexChanged = _callback; } while(0)
/**@}*/


/**
 * @ingroup SimplePIDFilter
 * Function to set as the filterpacket callback when using a SimplePIDFilter_t.
 * This is the function to use along with a SimplePIDFilter_t instance as the
 * user arg, when you want to use a simple filter callback that just checks the
 * PID of the packet is one of the specified PIDs.
 */
int PIDFilterSimpleFilter(PIDFilter_t *pidfilter, void *arg, uint16_t pid, TSPacket_t *packet);

#endif
