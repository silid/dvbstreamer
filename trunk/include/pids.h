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

pids.h

Manage PIDs.

*/
#ifndef _PIDS_H
#define _PIDS_H

#include <stdint.h>

#ifndef _DVBPSI_DESCRIPTOR_H_
#include <dvbpsi/descriptor.h>
#endif

#include "types.h"
#include "multiplexes.h"
#include "services.h"

#define PID_MASK     (0x1fff)
#define PID_STUFFING (0x1fff)
#define PID_ALL      (0x2000)
#define PID_INVALID  (0xFFFF)

/**
 * Structure describing a PID used by a program.
 */
typedef struct StreamInfo_t
{
    int pid;        /**< PID in question. */
    int type;       /**< Type of data this PID is used to transmit. */
    dvbpsi_descriptor_t *descriptors; /**< Linked list of descriptors for this stream */
}StreamInfo_t;

/**
 * Structure used to store a collection of streams for a specific program.
 */
typedef struct StreamInfoList_t
{
    int nrofStreams;         /**< Number of streams in the streams array */
    StreamInfo_t streams[0]; /**< Array of streams. */
}StreamInfoList_t;

/**
 * Structure describing a program (MPEG2).
 */
typedef struct ProgramInfo_s
{
    dvbpsi_descriptor_t *descriptors; /**< Linked list of descriptors from PMT */
    int pcrPID;                       /**< PCR PID */
    StreamInfoList_t *streamInfoList; /**< List of streams for this program */
}ProgramInfo_t;

/**
 * Create a ProgramInfo object with the specified number of entries for streams.
 * @param nrofStreams Number of streams to allocate.
 * @return A new ProgramInfo_t structure or null.
 */
ProgramInfo_t *ProgramInfoNew(int nrofStreams);

/**
 * Set the ProgramInfo for the specified service.
 *
 * @param service The service the pids belong to.
 * @param info The program info to set for the service.
 * @return 0 on success otherwise an sqlite3 error code.
 */
int ProgramInfoSet(Service_t *service, ProgramInfo_t *info);

/**
 * Retrieve the ProgramInfo for the specified service.
 * @param service The service to retrieve the pids for.
 * @return A ProgramInfo_t structure or null if no PIDs could be retreieved.
 */
ProgramInfo_t *ProgramInfoGet(Service_t *service);


/**
 * Remove all pids for the specified service.
 * @param service The service to remove pids from.
 * @return 0 on success otherwise an sqlite3 error code.
 */
int ProgramInfoRemove(Service_t *service);

#endif
