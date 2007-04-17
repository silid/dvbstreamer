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
#include "plugin.h"

/**
 * Enum describing the location of the main PID Filters in the PIDFilters array
 */
enum PIDFilterIndex
{
    PIDFilterIndex_PAT = 0, /**< Index of the PAT PID Filter. */
    PIDFilterIndex_PMT,     /**< Index of the PMT PID Filter. */
#ifdef ATSC_STREAMER
    PIDFilterIndex_PSIP,    /**< Index of the PSIP PID Filter. */
#else
    PIDFilterIndex_SDT,     /**< Index of the SDT PID Filter. */
    PIDFilterIndex_NIT,     /**< Index of the NIT PID Filter. */
    PIDFilterIndex_TDT,     /**< Index of the TDT PID Filter. */
#endif
    PIDFilterIndex_Count    /**< Number of main PID filters. */
};


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
 * Boolean used to signal the program to terminate.
 */
extern bool ExitProgram;

/**
 * Boolean indicating whether DVBStreamer is in daemon mode.
 */
extern bool DaemonMode;

/**
 * Boolean indicating whether to filter out non-printable characters from service names.
 */
extern bool FilterServiceNames;

/**
 * The character to replace non-printable characters with when filtering service names.
 */
extern char FilterReplacementChar;

/**
 * Constant for the PrimaryService output name
 */
extern char PrimaryService[];

/**
 * Directory path where DVBStreamer stores its data.
 */
extern char DataDirectory[];

/**
 * Writes any changes in the cache back to the database, ensuring the TS Filter is disabled.
 */
void UpdateDatabase();

#endif
