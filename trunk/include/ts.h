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

#include "dvbpsi/dvbpsi.h"
#include "types.h"
#include "dvbadapter.h"
#include "services.h"
#include "multiplexes.h"
#include "list.h"

/*------ Transport Stream Packet Structures and macros ----*/
/**
 * @defgroup TSPacket Transport Stream Packet Access
 * @{
 */

/**
 * Maximum number of pids in a transport stream.
 */
#define TS_MAX_PIDS 8192

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
/**
 * Boolean test to determine whether this packet is valid, transport_error_indicator 
 * is not set.
 * @param packet The packet to check.
 * @return True if the packet is valid, false otherwise.
 */
#define TSPACKET_ISVALID(packet) \
    (((packet).header[1] & 0x80) == 0x00)

/**
 * Retrieves the priority field of the packet.
 * @param packet The packet to check.
 * @return The packet priority.
 */
#define TSPACKET_GETPRIORITY(packet) \
    (((packet).header[1] & 0x20) >> 4)

/**
 * Set the priority field of the packet.
 * @param packet The packet to update.
 * @param priority Either 1 or 0 to indicate that this is a priority packet.
 */
#define TSPACKET_SETPRIORITY(packet, priority) \
    ((packet).header[1] = ((packet).header[1] & 0xdf) | (((priority) << 4) & 0x20))

/**
 * Retrieve whether the packet has an adaptation field.
 * @param packet The packet to check.
 * @return The adapatation field control flags.
 */
#define TSPACKET_GETADAPTATION(packet) \
    (((packet).header[3] & 0x30) >> 4)

/**
 * Set whether the packet has an adaptation field.
 * @param packet The packet to update.
 * @param adaptation The new adaptation field flags.
 */
#define TSPACKET_SETADAPTATION(packet, adaptation) \
    ((packet).header[3] = ((packet).header[3] & 0xcf) | (((adaptation) << 4) & 0x30))

/**
 * Retrieves the adaptation field length.
 * @param packet The packet to check.
 * @return The length of the adaptation field.
 */
#define TSPACKET_GETADAPTATION_LEN(packet) \
    ((packet).payload[0])


/**@}*/


/*---- Transport Stream Reader structure ----*/
/**
 * @defgroup TSReader Transport Stream Reader
 * @{
 */

typedef enum TSFilterEventType_e
{
    TSFilterEventType_MuxChanged,
    TSFilterEventType_StructureChanged
}TSFilterEventType_e;

struct TSFilterGroup_t;

typedef void (*TSFilterGroupEventCallback_t)(void *userArg, struct TSFilterGroup_t *group, TSFilterEventType_e event, void *details);

typedef void (*TSPacketFilterCallback_t)(void *userArg, struct TSFilterGroup_t *group, TSPacket_t *packet);
typedef struct TSPacketFilter_t
{
    uint16_t pid;

    TSPacketFilterCallback_t callback;
    void *userArg;
    struct TSFilterGroup_t *group;
    
    struct TSPacketFilter_t *next;

    struct TSPacketFilter_t *flNext;
}TSPacketFilter_t;

typedef struct TSSectionFilter_t
{
    uint16_t pid;
    int priority;
    dvbpsi_handle sectionHandle;
    struct TSFilterGroup_t *group;
    
    struct TSSectionFilter_t *next;
}TSSectionFilter_t;

#define TSSectFilterListFlags_PAYLOAD_START     1
#define TSSectFilterListFlags_PRIORITY_OVERRIDE 2

typedef struct TSSectionFilterList_t
{
    uint16_t pid;
    uint32_t flags;
    int priority;
    List_t *filters;
    dvbpsi_handle sectionHandle;
    TSPacketFilter_t packetFilter;
    struct TSReader_t *tsReader;
}TSSectionFilterList_t;

typedef struct TSFilterGroup_t
{
    char *name;
    char *type;
    struct TSReader_t *tsReader;           /**< TS Reader instance this filter group belongs to. */

    TSFilterGroupEventCallback_t eventCallback;
    void *userArg;

    TSSectionFilter_t *sectionFilters;
    TSPacketFilter_t *packetFilters;

    volatile unsigned long long packetsProcessed;
    volatile unsigned long long sectionsProcessed;

}TSFilterGroup_t;

#define TSREADER_PID_ALL 8192
#define TSREADER_NROF_FILTERS 8193
#define TSREADER_PIDFILTER_BUCKETS 8

/**
 * Structure describing a Transport Stream Filter instance.
 */
typedef struct TSReader_t
{
    bool quit;                          /**< Whether the filters thread should finish. */
    DVBAdapter_t *adapter;              /**< DVBAdapter packets should be read from */
    int notificationFds[2];             /**< File descriptors for waking up the reader thread. */
    bool notificationSent;
    pthread_t thread;                   /**< Thread used to read and process TS packets */
    bool enabled;                       /**< Whether packets should be read/processed */
    pthread_mutex_t mutex;              /**< Mutex used to protect access to this structure */
    bool multiplexChanged;              /**< Whether the multiplex has been changed. */
    Multiplex_t *multiplex;             /**< The multiplex the transport stream is coming from. */
    bool tsStructureChanged;            /**< Whether the underlying TS structure has changed. */

    volatile unsigned long long totalPackets; /**< Total number of packets processed by this instance. */
    volatile unsigned long bitrate;     /**< Approximate bit rate of the transport stream being processed. */

    bool promiscuousMode;               /**< Whether no filtering is applied at the adapter level all packets are available to PID filters. */
    List_t *groups;                     /**< List of TS Filter groups. */
    uint16_t currentlyProcessingPid;

    TSPacketFilter_t *packetFilters[TSREADER_NROF_FILTERS];

    List_t *sectionFilters;             /**< List of section filters that are awaiting scheduling */
    List_t *activeSectionFilters;       /**< List of active section filters. */
}
TSReader_t;

typedef struct TSFilterGroupStats_t
{
    char *name;
    unsigned long long packetsProcessed;
    unsigned long long sectionsProcessed;
    struct TSFilterGroupStats_t *next;
}TSFilterGroupStats_t;

typedef struct TSFilterGroupTypeStats_t
{
    char *type;
    TSFilterGroupStats_t *groups;
    struct TSFilterGroupTypeStats_t *next;
}TSFilterGroupTypeStats_t;
    
typedef struct TSReaderStats_t
{
    unsigned long long totalPackets; /**< Total number of packets processed by this instance. */
    unsigned long bitrate;           /**< Approximate bit rate of the transport stream being processed. */    
    TSFilterGroupTypeStats_t *types;
}TSReaderStats_t;

/**
 * Create a new TSReader_t instance that is processing packets from the specified
 * DVBAdapter instance.
 * @param adapter The DVBAdapter to read packets from.
 * @return A new TSReader_t instance or NULL if one could not be created.
 */
TSReader_t* TSReaderCreate(DVBAdapter_t *adapter);

/**
 * Stop and Destroy the specified TSReader_t instance.
 * @param reader The TSReader_t instance to destroy.
 */
void TSReaderDestroy(TSReader_t * reader);

/**
 * Enable/Disable processing of packets by the specified TSReader_t instance.
 * @param reader The instance to enable or disable packet processing on.
 * @param enable   TRUE to enable processing packets, FALSE to disable processing.
 */
void TSReaderEnable(TSReader_t * reader, bool enable);

/**
 * Extract information about the number of packets/sections processed and the approximate
 * TS bitrate.
 * @param reader The instance to extract the stats from.
 * @return A TSReaderStats_t instance containing available stats (use ObjectRefDec to free the structure).
 */
TSReaderStats_t *TSReaderExtractStats(TSReader_t *reader);

/**
 * Zero total packets and bit rate fields as well as all packet statistics fields
 * in all PIDFilters.
 * @param reader TSReader_t instance to zero the stats of.
 */
void TSReaderZeroStats(TSReader_t *reader);

/**
 * Informs all PID filters that the multiplex has changed to newmultiplex.
 * @param reader TSReader_t instance to inform.
 * @param newmultiplex The new multiplex that the DVB adapter is now tuned to.
 */
void TSReaderMultiplexChanged(TSReader_t *reader, Multiplex_t *newmultiplex);

/**
 * Lock access to the TSReader_t structure to this thread.
 * @param reader The instance to lock access to.
 */
#define TSReaderLock(reader)   pthread_mutex_lock(&(reader)->mutex)

/**
 * Unlock access to the TSReader_t structure.
 * @param reader The instance to unlock access to.
 */
#define TSReaderUnLock(reader) pthread_mutex_unlock(&(reader)->mutex)

TSFilterGroup_t* TSReaderCreateFilterGroup(TSReader_t *reader, const char *name, const char *type, TSFilterGroupEventCallback_t callback, void *userArg );
TSFilterGroup_t* TSReaderFindFilterGroup(TSReader_t *reader, const char *name, const char *type);
void TSFilterGroupDestroy(TSFilterGroup_t* group);
void TSFilterGroupRemoveAllFilters(TSFilterGroup_t* group);
void TSFilterGroupAddSectionFilter(TSFilterGroup_t *group, uint16_t pid, int priority, dvbpsi_handle handle);
void TSFilterGroupRemoveSectionFilter(TSFilterGroup_t *group, uint16_t pid);
bool TSFilterGroupAddPacketFilter(TSFilterGroup_t *group, uint16_t pid, TSPacketFilterCallback_t callback, void *userArg);
void TSFilterGroupRemovePacketFilter(TSFilterGroup_t *group, uint16_t pid);

/**@}*/
#endif
