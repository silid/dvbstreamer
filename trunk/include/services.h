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

services.h

Manage services and PIDs.

*/
#ifndef _SERVICES_H
#define _SERVICES_H
#include "types.h"
#include "multiplexes.h"
/**
 * @defgroup Service Service information
 * @{
 */

typedef enum RunningStatus_e
{
    RunningStatus_Undefined       = 0,
    RunningStatus_NotRunning      = 1,
    RunningStatus_StartsInSeconds = 2,
    RunningStatus_Pausing         = 3,
    RunningStatus_Running         = 4,
}RunningStatus_e;

/**
 * Structure desribing a digital TV service.
 */
typedef struct Service_t
{
    char *name;        /**< Name of the service. */
    int multiplexfreq; /**< Multiplex frequency this service is broadcast on. */
    int id;            /**< Service/Program ID of the service. */
    int pmtversion;    /**< Last processed version of the PMT. */
    int pmtpid;        /**< PID the PMT for this service is sent on. */

    /* Transient Fields - not stored in the database */
    bool conditionalaccess;        /**< Whether 1 or more streams in for this service are controlled by CA */
    RunningStatus_e runningstatus; /**< Running status of the service */
    bool eitpresentfollowing;      /**< Indicates whether EIT Present/Following information is available. */
    bool eitschedule;              /**< Indicates whether EIT schedule information is available. */
}
Service_t;

/**
 * Structure describing a PID used by a service.
 */
typedef struct PID_t
{
    int pid;        /**< PID in question. */
    int type;       /**< Type of data this PID is used to transmit. */
    int subtype;    /**< Additional information on the type (ie language code for audio) */
    int pmtversion; /**< Version of the services PMT that this PID appears in */
}
PID_t;

typedef void *ServiceEnumerator_t;

/**
 * Macro to compare 2 service structures.
 */
#define ServiceAreEqual(_service1, _service2) \
    (((_service1)->multiplexfreq == (_service2)->multiplexfreq) && \
    ((_service1)->id == (_service2)->id))

/**
 * Return the number of services in the database.
 * @return The number of services in the database.
 */
int ServiceCount();

/**
 * Retrieve the number of services on the specified multplex.
 * @param freq The multiplex to retrieve the service count for.
 * @return The number of services on the specified multiplex.
 */
int ServiceForMultiplexCount(int freq);

/**
 * Remove a service from the database.
 * @param service The service to remove from the database.
 * @return 0 on success, otherwise an SQLite error code.
 */
int ServiceDelete(Service_t  *service);
/**
 * Add a service to the database.
 * @param multiplexfreq The multiplex the service is broadcast on.
 * @param name Name of the service.
 * @param id The service/program id of the service.
 * @param pmtversion The version of the last PMT processed.
 * @param pmtpid The PID the service's PMT is transmitted on.
 * @return 0 on success, otherwise an SQLite error code.
 */
int ServiceAdd(int multiplexfreq, char *name, int id, int pmtversion, int pmtpid);

/**
 * Set the PMT version for the given service.
 * @param service The service to update.
 * @param pmtversion The version of the PMT to set.
 * @return 0 on success, otherwise an SQLite error code.
 */
int ServicePMTVersionSet(Service_t  *service, int pmtversion);

/**
 * Set the PMT PID for the given service.
 * @param service The service to update.
 * @param pmtpid The new PID of the PMT.
 * @return 0 on success, otherwise an SQLite error code.
 */
int ServicePMTPIDSet(Service_t  *service, int pmtpid);

/**
 * Set the service name for the given service.
 * @param service The service to update.
 * @param name The new name of the service.
 * @return 0 on success, otherwise an SQLite error code.
 */
int ServiceNameSet(Service_t  *service, char *name);

/**
 * Find the service with the given name.
 * The returned service should be free'd with ServiceFree when no longer required.
 * @param name The name of the service to find.
 * @return A service structure or NULL if the service was not found.
 */
Service_t *ServiceFindName(char *name);

/**
 * Find the service with the given id.
 * The returned service should be free'd with ServiceFree when no longer required.
 * @param id The id of the service to find.
 * @return A service structure or NULL if the service was not found.
 */
Service_t *ServiceFindId(Multiplex_t *multiplex, int id);

/**
 * Rerieve an enumerator for the entire service table.
 * @return A service enumerator or NULL if there is not enough memory.
 */
ServiceEnumerator_t ServiceEnumeratorGet();

/**
 * Rerieve an enumerator for the specified multiplex.
 * @return A service enumerator or NULL if there is not enough memory.
 */
ServiceEnumerator_t ServiceEnumeratorForMultiplex(int freq);

/**
 * Free an enumerator.
 */
void ServiceEnumeratorDestroy(ServiceEnumerator_t enumerator);

/**
 * Retrieve the next service from an enumerator.
 * @param enumerator The enumerator to retrieve the next service from.
 * @return A Service_t structure or NULL if there are no more services.
 */
Service_t *ServiceGetNext(ServiceEnumerator_t enumerator);

/**
 * Free the memory used by a Service_t instance.
 * @param service The structure instance to free.
 * @return 0 on success, otherwise an SQLite error code.
 */
void ServiceFree(Service_t *service);

/**
 * Add a PID to a service, for a given version of the PMT.
 * @param service The service to add the PID to.
 * @param pid The PID to add.
 * @param type The type of the data transmitted on the PID.
 * @param subtype Additional information on the type.
 * @param pmtversion The PMT version that this PID appeared in.
 * @return 0 on success, otherwise an SQLite error code.
 */
int ServicePIDAdd(Service_t *service, int pid, int type, int subtype, int pmtversion);

/**
 * Remove all the PIDs for a given service.
 * @param service The service of which to remove the PIDs.
 * @return 0 on success, otherwise an SQLite error code.
 */
int ServicePIDRemove(Service_t *service);

/**
 * Retrieve the number of PIDs for the specified service.
 * @param service The service to query.
 * @return The number of PIDs found for the service.
 */
int ServicePIDCount(Service_t *service);

/**
 * Retrieve all the PIDs for the specified service.
 * @param service The service to retrieve the PIDs of.
 * @param pids A pre-allocated array of PID_t structures to be filled in.
 * @param count Used to return the number of PIDs retrieved.
 * @return 0 on success, otherwise an SQLite error code.
 */
int ServicePIDGet(Service_t *service, PID_t *pids, int *count);
/** @} */
#endif
