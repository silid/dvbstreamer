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
#include "dbase.h"
#include "events.h"
#include "multiplexes.h"

/**
 * @defgroup Service Service information
 * Provides access to the underlying database to add/remove and modify services.
 *
 * \section events Events Exported
 *
 * \li \ref added Sent when a new service is added to the database.
 * \li \ref deleted Sent when a service is removed from the database.
 *
 * \subsection added Services.Added
 * This event is fired after a service has been added to the database. \n
 * \par
 * \c payload = A Service_t representing the service that has been added.
 *
 * \subsection deleted Services.Deleted
 * This event is fired before a service is removed from the database. \n
 * \par
 * \c payload = A Service_t representing the service that is to be removed.
 * @{
 */

/**
 * Enumeration to describe the type of a service.
 */
typedef enum {
    ServiceType_TV,     /**< Digital TV service type. */
    ServiceType_Radio,  /**< Digital Radio service type. */
    ServiceType_Data,   /**< Digital Data service type. */
    ServiceType_Unknown /**< Service type not known. */
}ServiceType;

/**
 * Structure describing a digital TV service.
 */
typedef struct Service_t
{
    char *name;             /**< Name of the service. */
    int multiplexUID;       /**< Multiplex frequency this service is broadcast on. */
    int networkId;          /**< Network ID the service is part of */
    int tsId;               /**< Transport stream ID the service is part of */
    int id;                 /**< Service/Program ID of the service. */
    int source;             /**< Source id of this service (for DVB this is the same 
                                 as the service ID for ATSC this is the channels 
                                 source id) */
    bool conditionalAccess; /**< Whether 1 or more streams in for this service are controlled by CA */
    ServiceType type;       /**< The type of this service (TV, Radio, Data ...) */
    int pmtVersion;         /**< Last processed version of the PMT. */
    int pmtPid;             /**< PID the PMT for this service is sent on. */
    int pcrPid;             /**< PID the PCR for this service is sent on. */

    char *provider;         /**< Provider of the service */
    char *defaultAuthority; /**< TVAnytime default authority for this service. */
}
Service_t;

/**
 * A collection of service.
 */
typedef struct ServiceList_s
{
    unsigned int nrofServices;
    Service_t *services[0];
}ServiceList_t;

/**
 * Handle for enumerating services.
 */
typedef void *ServiceEnumerator_t;

/**
 * Macro to compare 2 service structures.
 */
#define ServiceAreEqual(_service1, _service2) \
    (((_service1)->multiplexUID == (_service2)->multiplexUID) && \
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
int ServiceDeInit(void);


/**
 * Return the number of services in the database.
 * @return The number of services in the database.
 */
#define ServiceCount() DBaseCount(SERVICES_TABLE, NULL)

/**
 * Remove a service from the database.
 * @param service The service to remove from the database.
 * @return 0 on success, otherwise an SQLite error code.
 */
int ServiceDelete(Service_t  *service);

/**
 * Remove all service for a specific multiplex from the database.
 * @param mux The multiplex to remove all services for.
 * @return 0 on success, otherwise an SQLite error code.
 */
int ServiceDeleteAll(Multiplex_t *mux);

/**
 * Add a service to the database.
 * @param multiplexuid The multiplex the service is broadcast on.
 * @param name Name of the service.
 * @param id The service/program id of the service.
 * @param source The service/channels source id.
 * @param ca Whether the service is conditional access.
 * @param type The type of the service.
 * @param pmtversion The version of the last PMT processed.
 * @param pmtpid The PID the service's PMT is transmitted on.
 * @param pcrpid The PID the service's PCR is transmitted on.
 * @return 0 on success, otherwise an SQLite error code.
 */
int ServiceAdd(int multiplexuid, char *name, int id, int source, bool ca, 
                    ServiceType type, int pmtversion, int pmtpid, int pcrpid);

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
 * @param pcrpid The new PID of the PCR.
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
 * Set the service name for the given service.
 * @param service The service to update.
 * @param source The new source id of the service.
 * @return 0 on success, otherwise an SQLite error code.
 */
int ServiceSourceSet(Service_t  *service, int source);

/**
 * Set whether the service is conditional access of not.
 * @param service The service to update.
 * @param ca The new state of the service
 * @return 0 on success, otherwise an SQLite error code.
 */
int ServiceConditionalAccessSet(Service_t  *service, bool ca);
 
/**
 * Set the type of the specified service.
 * @param service The service to update.
 * @param type The new type of the service
 * @return 0 on success, otherwise an SQLite error code.
 */
int ServiceTypeSet(Service_t  *service, ServiceType type);
/**
 * Set the service provider name for the given service.
 * @param service The service to update.
 * @param provider The new provider name of the service.
 * @return 0 on success, otherwise an SQLite error code.
 */
int ServiceProviderSet(Service_t  *service, char *provider);

/**
 * Set the default authority for the given service.
 * @param service The service to update.
 * @param defaultAuthority The new authority of the service.
 * @return 0 on success, otherwise an SQLite error code.
 */
int ServiceDefaultAuthoritySet(Service_t  *service, char *defaultAuthority);

/**
 * Find a service based either on service name or fully qualified id.
 * @see ServiceFindName()
 * @see ServiceFindFQIDStr()
 * @param name Service name or fully qualified id.
 * @return A service structure or NULL if the service was not found.
 */
Service_t *ServiceFind(char *name);

/**
 * Find the service with the given name.
 * The returned service should be released with ServiceRefDec.
 * @param name The name of the service to find.
 * @return A service structure or NULL if the service was not found.
 */
Service_t *ServiceFindName(char *name);

/**
 * Find the service with the given id.
 * The returned service should be released with ServiceRefDec.
 * @param multiplex The multiplex the service is broadcast on.
 * @param id The id of the service to find.
 * @return A service structure or NULL if the service was not found.
 */
Service_t *ServiceFindId(Multiplex_t *multiplex, int id);

/**
 * Find the service based on the fully qualified ID.
 * The returned service should be released with ServiceRefDec.
 * @param networkId Network ID of the multiplex this service is on.
 * @param tsId TS ID of the multiplex this service is on.
 * @param serviceId ID of the service to find.
 * @return A service structure or NULL if the service was not found.
 */
Service_t *ServiceFindFQID(uint16_t networkId, uint16_t tsId, uint16_t serviceId);

/**
 * Find the service based on the fully qualified ID string.
 * A fully qualified ID string is in the form "<netId>.<tsId>.<serviceId>", 
 * where all the IDs are 16bit hex numbers.
 * The returned service should be released with ServiceRefDec.
 * @param FQIdStr String to extract the network, ts and service IDs from.
 * @return A service structure or NULL if the service was not found.
 */
Service_t *ServiceFindFQIDStr(char *FQIdStr);

/**
 * Retrieve an enumerator for the entire service table.
 * @return A service enumerator or NULL if there is not enough memory.
 * @deprecated Please use ServiceListAll(). 
 */
ServiceEnumerator_t ServiceEnumeratorGet();

/**
 * Retrieve a List_t object containing all services in the service table.
 * @return A List_t object of Service_t objects, use ObjectListFree() to 
 * free the list and the Service objects.
 */
List_t *ServiceListAll();

/**
 * Retrieve an enumerator for the specified multiplex.
 * @param multiplex The multiplex the service is broadcast on.
 * @return A service enumerator or NULL if there is not enough memory.
 * @deprecated Please use ServiceListForMultiplex.
 */
ServiceEnumerator_t ServiceEnumeratorForMultiplex(Multiplex_t *multiplex);

/**
 * Retrieve a List_t object containing all services for the specified multiplex.
 * @param multiplex The multiplex the service is broadcast on.
 * @return A List_t object of Service_t objects, use ObjectListFree() to 
 * free the list and the Service objects.
 */
List_t *ServiceListForMultiplex(Multiplex_t * multiplex);

/**
 * Retrieve an enumerator for the services known to be assosciated with the
 * given PID.  Optionally restrict the search to a given multiplex.
 * @param pid The PID to search for.
 * @param opt_multiplex Optional. The multiplex the service is broadcast on.
 * @return A service enumerator or NULL if there is not enough memory.
 * @deprecated Please use ServiceListForPID. 
 */
ServiceEnumerator_t ServiceFindByPID(int pid, Multiplex_t *opt_multiplex);

/**
 * Retrieve a List_t object containing all services  known to be assosciated 
 * with the given PID.  Optionally restrict the search to a given multiplex.
 * @param pid The PID to search for. 
 * @param opt_multiplex Optional. The multiplex the service is broadcast on. 
 * @return A List_t object of Service_t objects, use ObjectListFree() to 
 * free the list and the Service objects.
 */
List_t *ServiceListForPID(int pid, Multiplex_t *opt_multiplex);

/**
 * Retrieve an enumerator for the names that match the query string.
 * This function uses the SQL LIKE syntax for the query string.
 * @param query An SQL LIKE formated string of the name to search for.
 * @return A service enumerator or NULL if there is not enough memory.
 * @deprecated Please use ServiceListForNameLike.
 */
ServiceEnumerator_t ServiceQueryNameLike(char *query);

/**
 * Retrieve a List_t object containing all services with names that match the 
 * query string.
 * This function uses the SQL LIKE syntax for the query string.
 * @param query An SQL LIKE formated string of the name to search for.
 * @return A List_t object of Service_t objects, use ObjectListFree() to 
 * free the list and the Service objects.
 */
List_t *ServiceListForNameLike(char * query);

/**
 * Free an enumerator.
 */
void ServiceEnumeratorDestroy(ServiceEnumerator_t enumerator);

/**
 * Retrieve the next service from an enumerator.
 * The returned service should be released with ServiceRefDec.
 * @param enumerator The enumerator to retrieve the next service from.
 * @return A Service_t structure or NULL if there are no more services (with 
 * the reference count set to 1)
 * @deprecated Please use the ServiceList* equivalent functions.
 */
Service_t *ServiceGetNext(ServiceEnumerator_t enumerator);

/**
 * Create a new service object.
 */
#define ServiceNew() (Service_t*)ObjectCreateType(Service_t)

/**
 * Increment the references to the specified service object.
 * @param __service The service instance to increment the reference count of.
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
 * @param __service The service instance to decrement the reference count of.
 */
#define ServiceRefDec(__service) \
        do{ \
            if ((__service)) \
            { \
                ObjectRefDec(__service); \
            } \
        }while(0)

/**
 * Use as the toString parameter when registering an event where the payload will
 * be a service object.
 */
int ServiceEventToString(yaml_document_t *document, Event_t event,void * payload);

/** @} */
#endif
