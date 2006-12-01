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
    PIDFilterIndex_SDT,     /**< Index of the SDT PID Filter. */
    PIDFilterIndex_NIT,     /**< Index of the NIT PID Filter. */
    PIDFilterIndex_TDT,     /**< Index of the TDT PID Filter. */

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
 * Register a function to be called when the primary output service is changed.
 * @param callback The function to call.
 */
void ChannelChangedRegisterCallback(PluginChannelChanged_t callback);

/**
 * UnRegister a function to be called when the primary output service is changed.
 * @param callback The function to call.
 */
void ChannelChangedUnRegisterCallback(PluginChannelChanged_t callback);

/**
 * Set the current service being stream to the primary output.
 * Changing this can cause a re-tune!.
 * @param name Name of the new service.
 * @return The new services Service_t structure or NULL if the service was not found.
 */
extern Service_t *SetCurrentService(char *name);

/**
 * Set the current multiplex. Retunes to the new multiplex, TS stats are reset,
 * ChannelChange listeners are informed and the primary service is set to NULL.
 * Primary use for this is scanning a multiplex for services.
 * @param multiplex The new multiplex to tune to.
 */
extern void SetMultiplex(Multiplex_t *multiplex);

/**
 * Writes any changes in the cache back to the database, ensuring the TS Filter is disabled.
 */
void UpdateDatabase();

#endif
