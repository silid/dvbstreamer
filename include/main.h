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

main.h

Entry point to the application.

*/
#ifndef _MAIN_H
#define _MAIN_H

#include "types.h"
#include "dvb.h"
#include "ts.h"
#include "services.h"
#include "multiplexes.h"

/**
 * Enum describing the location of the main PID Filters in the PIDFilters array
 */
enum PIDFilterIndex
{
    PIDFilterIndex_PAT = 0, /**< Index of the PAT PID Filter. */
    PIDFilterIndex_PMT,     /**< Index of the PMT PID Filter. */
    PIDFilterIndex_SDT,     /**< Index of the SDT PID Filter. */

    PIDFilterIndex_Count    /**< Number of main PID filters. */
};

/**
 * The multiplex of the current service.
 */
extern volatile Multiplex_t *CurrentMultiplex;

/**
 * The currently stream service, Service_t structure.
 */
extern volatile Service_t *CurrentService;

/**
 * Array containing the main PID filters (PAT, PMT and SDT).
 * Use the PIDFilterIndex enum to access this array.
 */
extern PIDFilter_t  *PIDFilters[];

/**
 * The TSFilter_t instance being used by the application
 */
extern TSFilter_t   *TSFilter;

/**
 * The DVBAdapter_t instance being used by the application.
 */
extern DVBAdapter_t *DVBAdapter;

/**
 * Set the current service being stream to the primary output.
 * Changing this can cause a re-tune!.
 * @param name Name of the new service.
 * @return The new services Service_t structure or NULL if the service was not found.
 */
extern Service_t *SetCurrentService(char *name);

/**
 * Boolean used to signal the program to terminate.
 */
extern bool ExitProgram;

/**
 * Boolean indicating whether DVBStreamer is in daemon mode.
 */
extern bool DaemonMode;

/**
 * Constant for the PrimaryService output name
 */
extern char PrimaryService[];

#endif
