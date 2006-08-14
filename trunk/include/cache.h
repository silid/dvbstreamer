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

cache.h

Caches service and PID information from the database for the current multiplex.

*/

#ifndef _CACHE_H
#define _CACHE_H

#include "ts.h"
#include "multiplexes.h"
#include "services.h"

/** @defgroup Cache Service Cache Management
 * @{
 */

/**
 * @internal
 * Initialise the cache module.
 */
int CacheInit();

/**
 * @internal
 * De-initialise the cache module, release all services and pids.
 */
void CacheDeInit();

/**
 * Load the cache with all the service in the specfied multiplex.
 * @param multiplex The multiplex to load all the services for.
 * @return 0 on success or an SQLite error code.
 */
int CacheLoad(Multiplex_t *multiplex);

/**
 * Write any changes in the cache, back to the database.
 */
void CacheWriteback();

/**
 * Find a service in the cache with the specified id.
 * @param id The service/program id to search for.
 * @return A Service_t instance or NULL if not found.
 */
Service_t *CacheServiceFindId(int id);

/**
 * Find a service with a given name, first searching in the cache and then in
 * the database.
 * @param name Name of the service to look for.
 * @param multiplex Used to store the multiplex the service belongs to.
 * @return A Service_t instance, that if multiplex == CurrentMultiplex is in the
 * cache or is from the database and should be free'd. If NULL is returned the
 * service could not be found in the database or the cache.
 */
Service_t *CacheServiceFindName(char *name, Multiplex_t **multiplex);

/**
 * Retrieve all the services currently in the cache.
 * @param count Used to store the number of services in the cache.
 * @return An array of pointers to Service_t instances.
 */
Service_t **CacheServicesGet(int *count);

/**
 * Retrieve the PIDs for a given service.
 * @param service Service to retrieve the PIDs for.
 * @param count Used to store the number of PIDs retrieved.
 * @return An array of PID_t structures, should be free'd after use.
 */
PID_t *CachePIDsGet(Service_t *service, int *count);

/**
 * Update the specified Multiplex's pat version and TS id.
 * @param multiplex The multiplex to update.
 * @param patversion The new pat version.
 * @param tsid The new TS ID.
 */
void CacheUpdateMultiplex(Multiplex_t *multiplex, int patversion, int tsid);

/**
 * Update the specified Multiplex's network id.
 * @param multiplex The multiplex to update.
 * @param netid The network id to set.
 */
void CacheUpdateNetworkId(Multiplex_t *multiplex, int netid);

/**
 * Update the cached service with a new PMT PID.
 * @param service The service to update.
 * @param pmtpid The new PMT PID.
 */
void CacheUpdateService(Service_t *service, int pmtpid);

/**
 * Update the cached service with a new name.
 * @param service The service to update.
 * @param name The new name.
 */
void CacheUpdateServiceName(Service_t *service, char *name);

/**
 * Update the PIDs for the specified service.
 * @param service The service to update.
 * @param pids An array of new PIDs.
 * @param count The number of PIDs.
 * @param pmtversion The new PMT version.
 */
void CacheUpdatePIDs(Service_t *service, PID_t *pids, int count, int pmtversion);

/**
 * Add a new Service to the cache.
 * @param id The new service/program id.
 */
Service_t *CacheServiceAdd(int id);

/**
 * Delete a service from the cache.
 * @param service The service to be deleted when CacheWriteback() is called.
 */
void CacheServiceDelete(Service_t *service);

/** @} */
#endif
