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

/**
 * Structure describing a PID used by a service.
 */
typedef struct PID_t
{
    int pid;        /**< PID in question. */
    int type;       /**< Type of data this PID is used to transmit. */
    int subType;    /**< Additional information on the type (ie language code for audio) */
    int pmtVersion; /**< Version of the services PMT that this PID appears in */
    dvbpsi_descriptor_t *descriptors; /**< Linked list of descriptors for this PID */
}
PID_t;

/**
 * Structure used to store a collection of PIDs for a specific service.
 */
typedef struct PIDList_t
{
    int count;     /**< Number of pids in the pids array */
    PID_t pids[0]; /**< Array of pids. */
}
PIDList_t;

/**
 * Create a PID list with the specified number of pid entries.
 * @param count Number of pids in the list.
 * @return A new PIDList_t structure containing the specified number of entries or null.
 */
PIDList_t *PIDListNew(int count);

/**
 * Free a list of PIDs and their descriptors.
 * @param pids The PID List to free.
 */
void PIDListFree(PIDList_t *pids);

/**
 * Create a deep copy of a pid list.
 * @param pids The pids list to clone.
 * @return A new PIDList_t object or NULL.
 */
PIDList_t *PIDListClone(PIDList_t *pids);

/**
 * Set the list of PIDs for the specified service.
 *
 * @param service The service the pids belong to.
 * @param pids The list of pids to set for the service.
 * @return 0 on success otherwise an sqlite3 error code.
 */
int PIDListSet(Service_t *service, PIDList_t *pids);

/**
 * Retrieve the list of pids for the specified service.
 * @param service The service to retrieve the pids for.
 * @return A PIDList_t structure or null if no PIDs could be retreieved.
 */
PIDList_t *PIDListGet(Service_t *service);

/**
 * Retrieve the number of pids for the specified service.
 * @param service The service to retrieve the pids for.
 * @return The number of PIDs for the specified service or -1 on error.
 */
int PIDListCount(Service_t *service);

/**
 * Remove all pids for the specified service.
 * @param service The service to remove pids from.
 * @return 0 on success otherwise an sqlite3 error code.
 */
int PIDListRemove(Service_t *service);

#endif
