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
#include "pids.h"

/** 
 * @defgroup DatabaseCache Database Cache Management
 * This module is used to allow changes to be recorded by the PID filters running
 * in the TS Filter thread without having the thread halted while the database 
 * file is accessed.
 *
 * @note Functions in this module should only be used from within the TS Filter 
 * thread, all other threads should access the database through Services and 
 * Multiplexes modules
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
 * Retrieve the Multiplex that the cache is currently managing the services of.
 * @return A Multiplex_t instance or NULL if the cache has not be loaded.
 */
Multiplex_t *CacheMultiplexGet(void);

/**
 * Find a service in the cache by either name or fully qualified id (i.e. 
 * \<network id\>.\<ts id\>.\<service id\> where ids are in hex).
 *
 * @param name Name of the service or fully qualified id 
 * @return A Service_t instance or NULL if not found.
 */
Service_t *CacheServiceFind(char *name);

/**
 * Find a service in the cache with the specified id.
 * @param id The service/program id to search for.
 * @return A Service_t instance or NULL if not found.
 */
Service_t *CacheServiceFindId(int id);

/**
 * Find a service with a given name in the cache.
 * @param name Name of the service to look for.
 * @return A Service_t instance or NULL if not found in the cache.
 */
Service_t *CacheServiceFindName(char *name);

/**
 * Retrieve all the services currently in the cache and locks the cache to
 * prevent updates to the list. CacheServicesRelease() should be called when the
 * list is no longer needed.
 *
 * @param count Used to store the number of services in the cache.
 * @return An array of pointers to Service_t instances.
 */
Service_t **CacheServicesGet(int *count);

/**
 * Releases the services retieved by CacheServiesGet and allows updates to the
 * cache.
 */
void CacheServicesRelease(void);

/**
 * Retrieve the PIDs for a given service and locks the cache to prevent updates.
 * ObjectRefDec() should be called on the list when it is no longer needed.
 *
 * @param service Service to retrieve the PIDs for.
 * @return A ProgramInfo_t structure or NULL if no information is available.
 */
ProgramInfo_t* CacheProgramInfoGet(Service_t *service);


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
void CacheUpdateServicePMTPID(Service_t *service, int pmtpid);

/**
 * Update the cached service with a new name.
 * @param service The service to update.
 * @param name The new name.
 */
void CacheUpdateServiceName(Service_t *service, char *name);

/**
 * Update the cached service with a new provider.
 * @param service The service to update.
 * @param provider The new provider name.
 */
void CacheUpdateServiceProvider(Service_t *service, char *provider);

/**
 * Update the cached service with a new default authority, used by TVAnytime.
 * @param service The service to update.
 * @param defaultAuthority The new default authority.
 */
void CacheUpdateServiceDefaultAuthority(Service_t *service, char *defaultAuthority);

/**
 * Update the cached service with a new source id.
 * @param service The service to update.
 * @param source The new source id.
 */
void CacheUpdateServiceSource(Service_t *service, uint16_t source);

/**
 * Update the cached service with the new CA state of the service.
 * @param service The service to update.
 * @param ca The new CA state.
 */
void CacheUpdateServiceConditionalAccess(Service_t *service, bool ca);

/**
 * Update the cached service with the new type of the service.
 * @param service The service to update.
 * @param type The new type of the service.
 */
void CacheUpdateServiceType(Service_t *service, ServiceType type);

/**
 * Update the Program Info for the specified service.
 * @param service The service to update.
 * @param info A ProgramInfo_t object containing the new information.
 */
void CacheUpdateProgramInfo(Service_t *service, ProgramInfo_t *info);

/**
 * Add a new Service to the cache.
 * @param id The new service/program id.
 * @param source The source Id for EPG information.
 */
Service_t *CacheServiceAdd(int id, int source);

/**
 * Update the 'seen' state of the service. 
 * If a service is seen in the PAT but not in the SDT/VCT or vice versa the service
 * still exists, but if the service is no longer seen in the PAT and SDT/VCT, the
 * service no longer exists and should be deleted.
 * @param service The service to update the 'seen' status of.
 * @param seen Whether the service has been seen or not.
 * @param pat If the services was (not) seen in the PAT, but in the SDT/VCT.
 * @return True if the service still exists, False otherwise.
 */
bool CacheServiceSeen(Service_t *service, bool seen, bool pat);

/**
 * Delete a service from the cache.
 * @param service The service to be deleted when CacheWriteback() is called.
 */
void CacheServiceDelete(Service_t *service);

/** @} */
#endif
