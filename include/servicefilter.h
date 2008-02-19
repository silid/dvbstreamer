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

servicefilter.h

Filter all packets for a service include the PMT, rewriting the PAT sent out in
the output to only include this service.

*/
#ifndef _SERVICFILTER_H
#define _SERVICFILTER_H
#include "ts.h"
#include "deliverymethod.h"
/**
 * @defgroup ServiceFilter Service Filter management functions and constants.
 * @{
 */
 
/**
 * String constant used to signify that a PIDFilter instance is being used as a 
 * service filter.
 */
extern char ServicePIDFilterType[];

/**
 * Creates a new Service Filter linked to the supplied TS Filter and return the
 * PIDFilter_t instance used to filter the TS.
 * @param tsFilter The TS Filter to link the ServiceFilter to.
 * @return The PIDFilter_t instance that will be used to filter PIDs.
 */
PIDFilter_t *ServiceFilterCreate(TSFilter_t *tsfilter);

/**
 * Destroy a service filter created by ServiceFilterCreate().
 * @param filter The service filter to destroy.
 */
void ServiceFilterDestroy(PIDFilter_t *filter);

/**
 * Destroy all service filters linked to the specified TS Filter.
 * @param tsFilter The TS Filter to remove all ServiceFilters from.
 */
void ServiceFilterDestroyAll(TSFilter_t *tsFilter);

/**
 * Set the service filtered by the specified service filter.
 * @param filter The service filter to set the service being filtered on or 
 *               NULL to stop filtering.
 * @param service The new service to filter.
 */
void ServiceFilterServiceSet(PIDFilter_t *filter, Service_t *service);
/**
 * Get the service being filtered by the specified service filter.
 * @param filter The service filter to retrieve the service from.
 * @return A Service_t instance or NULL if no service is being filtered.
 */
Service_t *ServiceFilterServiceGet(PIDFilter_t *filter);

/**
 * Set whether to filter out any PIDs that are not being used by Audio/Video or 
 * subtitle (AVS) streams. This also rewrites the PMT (when enabled) to contain 
 * only the first available video/audio/subtitle PIDs.
 * @param filter The service filter to change the AVS only setting on.
 * @param enable TRUE to enable AVS only, FALSE to disable and filter all PIDS 
 *               related to the service.
 */
void ServiceFilterAVSOnlySet(PIDFilter_t *filter, bool enable);

/**
 * Get whether the specified service filter is only filtering Audio/Video or 
 * subtitle PIDs.
 * @param filter The service filter to check.
 * @return TRUE if only AVS PIDs are being filtered, FALSE otherwise.
 */
bool ServiceFilterAVSOnlyGet(PIDFilter_t *filter);

/**
 * Set the delivery method associated with the specified service filter.
 * @param filter The service filter to change the delivery method of.
 * @param instance The new delivery method to set.
 */
void ServiceFilterDeliveryMethodSet(PIDFilter_t *filter, DeliveryMethodInstance_t *instance);
/**
 * Get the delivery method associated with the specified service filter.
 * @param filter The service filter to interrogate.
 * @return A DeliveryMethodInstance_t or NULL if not delivery method has been set.
 */
DeliveryMethodInstance_t * ServiceFilterDeliveryMethodGet(PIDFilter_t *filter);

/**@}*/
#endif
