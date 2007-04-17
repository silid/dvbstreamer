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

tuning.h

Frontend Tuner control.

*/
#ifndef _TUNING_H
#define _TUNING_H

#include <stdint.h>

#include "types.h"
#include "dvb.h"
#include "multiplexes.h"
#include "services.h"
#include "plugin.h"

/** @defgroup Tuning Frontend Control Functions
 * @{
 */
 
/**
 * Initialise the Tuning module for use.
 * @return 0 on success.
 */
int TuningInit(void);
/**
 * Deinitialise the Tuning module.
 * @return 0 on success.
 */ 
int TuningDeinit(void);

/**
 * Register a Channel Changed callback.
 * @param callback The callback function to register.
 */
void TuningChannelChangedRegisterCallback(PluginChannelChanged_t callback);
/**
 * Unregister a Channel Changed callback.
 * @param callback The callback function to unregister.
 */
void TuningChannelChangedUnRegisterCallback(PluginChannelChanged_t callback);

/**
 * Retrieve the current (primary) service.
 * @return The current service or NULL if no service is selected. Service should
 * be released with a call to ServiceRefDec when no longer needed.
 */
Service_t *TuningCurrentServiceGet(void);
/** 
 * Set the current (primary) service to the one specified.
 * @param name Name of the service to select.
 * @return The current service or NULL if no service is selected. Service should
 * be released with a call to ServiceRefDec when no longer needed.
 */
Service_t *TuningCurrentServiceSet(char *name);

/**
 * Retrieve the multiplex the frontend is currently tuned to.
 * @return The current multiplex or NULL if no service/multiplex has been selected.
 * Multiplex should be released when no longer needed with a call to MultiplexRefDec.
 */
Multiplex_t *TuningCurrentMultiplexGet(void);

/**
 * Set the current multiplex (independently of the service).
 * This function is useful for scanning as no service is required to tune to 
 * a specific multiplex.
 * @param multiplex The new multiplex to tune to.
 */
void TuningCurrentMultiplexSet(Multiplex_t *multiplex);

/** @} */
#endif
