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

#include <stdint.h>

#ifndef _DVBPSI_DESCRIPTOR_H_
#include <dvbpsi/descriptor.h>
#endif

#include "types.h"
#include "objects.h"
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

#define SERVICE_MAX_NAME_LEN (256)
/**
 * Structure desribing a digital TV service.
 */
typedef struct Service_t
{
    char name[SERVICE_MAX_NAME_LEN]; /**< Name of the service. */
    int multiplexFreq; /**< Multiplex frequency this service is broadcast on. */
    int id;            /**< Service/Program ID of the service. */
    int pmtVersion;    /**< Last processed version of the PMT. */
    int pmtPid;        /**< PID the PMT for this service is sent on. */
    int pcrPid;        /**< PID the PCR for this service is sent on. */

    /* Transient Fields - not stored in the database */
    bool conditionalAccess;        /**< Whether 1 or more streams in for this service are controlled by CA */
    RunningStatus_e runningStatus; /**< Running status of the service */
    bool eitPresentFollowing;      /**< Indicates whether EIT Present/Following information is available. */
    bool eitSchedule;              /**< Indicates whether EIT schedule information is available. */
}
Service_t;

typedef void *ServiceEnumerator_t;

/**
 * Macro to compare 2 service structures.
 */
#define ServiceAreEqual(_service1, _service2) \
    (((_service1)->multiplexFreq == (_service2)->multiplexFreq) && \
    ((_service1)->id == (_service2)->id))

/**
 * Initialise the service module for use.
 * @return 0 on success.
 */
int ServiceInit(void);

/**
 * Release resources used by the service module.
 * @return 0 on success.
 */
int ServiceDeinit(void);


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
 * @param pcrpid The PID the service's PCR is transmitted on.
 * @return 0 on success, otherwise an SQLite error code.
 */
int ServiceAdd(int multiplexfreq, char *name, int id, int pmtversion, int pmtpid, int pcrpid);

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
 * Set the PCR PID for the given service.
 * @param service The service to update.
 * @param pmtpid The new PID of the PCR.
 * @return 0 on success, otherwise an SQLite error code.
 */
int ServicePCRPIDSet(Service_t  *service, int pcrpid);

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
 * @return A Service_t structure or NULL if there are no more services (with 
 * the reference count set to 1)
 */
Service_t *ServiceGetNext(ServiceEnumerator_t enumerator);

/**
 * Create a new service object.
 */
#define ServiceNew() (Service_t*)ObjectCreateType(Service_t)

/**
 * Increment the references to the specified service object.
 * @param service The service instance to increment the reference count of.
 */
#define ServiceRefInc(__service) \
        do{ \
            if ((__service)) \
            { \
                ObjectRefInc(__service); \
            } \
        }while(0)

/**
 * Decrement the references of the specified service object. If the reference 
 * count reaches 0 the instance is free'd.
 * @param service The service instance to decrement the reference count of.
 */
#define ServiceRefDec(__service) \
        do{ \
            if ((__service)) \
            { \
                ObjectRefDec(__service); \
            } \
        }while(0)

/** @} */
#endif
