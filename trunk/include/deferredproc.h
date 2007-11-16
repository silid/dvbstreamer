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

deferredproc.h

Deferred Processing.

*/

#ifndef _DVBSTREAMER_DEFERREDPROC_H
#define _DVBSTREAMER_DEFERREDPROC_H

#include "types.h"

/**
 * @defgroup DeferredProc Deferred Processing
 *@{
 */

/**
 * Function pointer to a function to be called by the deferred processing thread.
 */
typedef void (*DeferredProcessor_t)(void*);

/**
 * Initialise the deferred processing module.
 * @return 0 on success.
 */
int DeferredProcessingInit(void);

/**
 * Deinitialise the deferred processing module. Any job waiting to be processed 
 * will be dropped.
 */
void DeferredProcessingDeinit(void);

/**
 * Add a job to the queue of jobs to be executed in the deferred processing thread.
 * The function pointed to by processor will be called at an indeterminate time 
 * with the argument specified by arg.
 * @param processor Function pointer to function to call in the deferred processing thread.
 * @param arg Argument to be passed to processor when called. This must have 
 * been allocated using the Object memory system as the reference count will be 
 * incremented on adding to the queue and decremented if the job is still on the
 * queue when the module is deinitialised.
 */
void DeferredProcessingAddJob(DeferredProcessor_t processor, void *arg);

/** @} */
#endif


