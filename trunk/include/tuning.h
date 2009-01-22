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
#include "dvbadapter.h"
#include "multiplexes.h"
#include "services.h"
#include "plugin.h"

/** @defgroup Tuning High level Frontend Control
 * This module controls tuning of the frontend and attempts to keep retunes to a minimum.
 *
 * When an new service is selected with TuningCurrentServiceSet the new service 
 * checked to see if it is encapsulated in the currently tuned multiplex. If it 
 * is the frontend is not retuned, if the new service is not in the current 
 * multiplex then the frontend is tuned to the multiplex the service is a member of.
 *
 * \section events Events Exported
 * 
 * \li \ref servicechanged Fired when the primary service is changed.
 * \li \ref multiplexchanged Fired when the current multiplex changes.
 *
 * \subsection servicechanged Tuning.ServiceChanged
 * Fired when the primary service filter service is changed. \n
 * \par 
 * \c payload = The new Service_t. \n
 *
 * \subsection multiplexchanged Tuning.MultiplexChanged
 * Fired when the tuned multiplex changes. \n
 * \par 
 * \c payload = The new Multiplex_t.
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
int TuningDeInit(void);

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
 * Note: Make sure you have called UpdateDatabase() prior to calling this function
 * otherwise any changes to the multiplex/services will be lost.
 *
 * @param service The new service to tune to.
 */
void TuningCurrentServiceSet(Service_t *service);

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
