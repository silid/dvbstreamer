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

multiplexes.h

Manage multiplexes and tuning parameters.

*/
#ifndef _MULTIPLEX_H_
#define _MULTIPLEX_H_
#include <sys/types.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>
#include "objects.h"
#include "dvbadapter.h"
#include "list.h"
#include "dbase.h"
#include "events.h"

/**
 * @defgroup Multiplex Multiplex information
 * This module is used to store and retrieve the multiplex information in the 
 * adapters database.
 * @{
 */

/**
 * Structure describing a multiplex.
 */
typedef struct Multiplex_t
{    
    int uid;        /**< Unique ID for this multiplex */
    int freq;       /**< Frequency the multiplex is broadcast on. */
    int tsId;       /**< Transport Stream ID. */
    int networkId;  /**< Network ID */
    fe_type_t type; /**< The type of frontend used to receive this transport stream. */
    int patVersion; /**< Last processed version of the PAT */
}
Multiplex_t;

/**
 * Handle for enumerating multiplexes.
 */
typedef void * MultiplexEnumerator_t;

/**
 * Macro to compare 2 Multiplex_t structures.
 */
#define MultiplexAreEqual(_multiplex1, _multiplex2) \
    ((_multiplex1)->uid == (_multiplex2)->uid)

/**
 * Initialise the multiplex module for use.
 * @return 0 on success.
 */
int MultiplexInit(void);

/**
 * Release resources used by the multiplex module.
 * @return 0 on success.
 */
int MultiplexDeInit(void);

/**
 * Number of multiplexes stored in the database.
 * @return The number of multiplexes in the database.
 */
#define MultiplexCount() DBaseCount(MULTIPLEXES_TABLE)

/**
 * Retrieve a Multiplex_t structure for the string mux.
 * mux is tried as (in order) UID, netid.tsid and finally frequency.
 * @return A Mulitplex_t or NULL if the frequency could not be found.
 */
Multiplex_t *MultiplexFind(char *mux);

/**
 * Retrieve the Multiplex_t structure for the UID.
 * The returned structured should be released using MultiplexRefDec.
 * @param uid Unique ID of the multiplex to retrieve.
 * @return A Mulitplex_t or NULL if the frequency could not be found.
 */
Multiplex_t *MultiplexFindUID(int uid);

/**
 * Retrieve the Multiplex_t structure for the network and TS id.
 * The returned structured should be released using MultiplexRefDec.
 * @param netid Network id to find.
 * @param tsid Transport stream id to find.
 * @return A Mulitplex_t or NULL if the frequency could not be found.
 */
Multiplex_t *MultiplexFindId(int netid, int tsid);

/**
 * Retrieve the first Multiplex_t structure for the given frequency.
 * The returned structured should be released using MultiplexRefDec.
 * @param freq Frequency of the multiplex to retrieve.
 * @return A Mulitplex_t or NULL if the frequency could not be found.
 */
Multiplex_t *MultiplexFindFrequency(int freq);

/**
 * Retrieve the first Multiplex_t structure for the given frequency range, ie
 * freq - plusminus >= mux <= freq + plusminus.
 * The returned structured should be released using MultiplexRefDec.
 * @param freq Frequency of the multiplex to retrieve.
 * @param plusminus The range above and below the frequency to match.
 * @return A Mulitplex_t or NULL if the frequency could not be found.
 */
Multiplex_t *MultiplexFindFrequencyRange(int freq, int plusminus);

/**
 * Retrieve the first Multiplex_t structure for the given frequency which also 
 * matches the supplied DiSEqC settings.
 * The returned structured should be released using MultiplexRefDec.
 * @param freq Frequency of the multiplex to retrieve.
 * @param diseqc The DiSEqC settings to match.
 * @return A Mulitplex_t or NULL if the frequency could not be found.
 */
Multiplex_t *MultiplexFindDVBSMultiplex(int freq, DVBDiSEqCSettings_t *diseqc);

/**
 * Retrieve an enumerator for all the multiplexes in the database.
 * @return An enumerator instance or NULL if there was not enough memory.
 */
MultiplexEnumerator_t MultiplexEnumeratorGet();

/**
 * Retrieve a List_t object containing all the multiplexes in the database.
 * @return A List_t instance containing Multiplex_t objects or NULL if there was
 * not enough memory. Use ObjectListFree() to free all the multiplex objects and 
 * the list.
 */
List_t *MultiplexListAll();

/**
 * Destroy an enumerator return by MultiplexEnumeratorGet().
 * @param enumerator The enumerator to free.
 */
void MultiplexEnumeratorDestroy(MultiplexEnumerator_t enumerator);

/**
 * Retrieve the next multiplex from an enumerator.
 * The returned structured should be free'd using free().
 * @param enumerator The enumerator to retrieve the next multiplex from.
 * @return A Multiplex_t instance or NULL if there are no more multiplexes.
 */
Multiplex_t *MultiplexGetNext(MultiplexEnumerator_t enumerator);

/**
 * Retrieve the frontend parameters for the given multiplex.
 * @param multiplex The multiplex to retrieve the frontend parameters for.
 * @param feparams Used to store the frontend parameters.
 * @param diseqc DiSEqC parameters, may be NULL for non-satellite frontends.
 * @return 0 on success, otherwise an SQLite error code.
 */
int MultiplexFrontendParametersGet(Multiplex_t *multiplex, struct dvb_frontend_parameters *feparams, DVBDiSEqCSettings_t *diseqc);

/**
 * Add a multiplex to the database.
 * @param type The type of frontend used to receive this transport stream.
 * @param feparams The parameters to pass to the frontend to tune to the new TS.
 * @param diseqc Any DiSEqC properties required to tune.
 * @param mux On exit, if successful, contains a pointer to a Multiplex_t object.
 * @return 0 on success, otherwise an SQLite error code.
 */
int MultiplexAdd(fe_type_t type, struct dvb_frontend_parameters *feparams, DVBDiSEqCSettings_t *diseqc, Multiplex_t **mux);

/**
 * Remove a multiplex and all its services from the database.
 * @param multiplex The multiplex to delete.
 * @return 0 on success, otherwise an SQLite error code.
 */
int MultiplexDelete(Multiplex_t *multiplex);

/**
 * Set the PAT version of a multiplex.
 * @param multiplex The multiplex to update.
 * @param patversion The version of the PAT to set.
 * @return 0 on success, otherwise an SQLite error code.
 */
int MultiplexPATVersionSet(Multiplex_t *multiplex, int patversion);

/**
 * Set the TS ID of the multiplex.
 * @param multiplex The multiplex to update.
 * @param tsid The ID of TS set.
 * @return 0 on success, otherwise an SQLite error code.
 */
int MultiplexTSIdSet(Multiplex_t *multiplex, int tsid);

/**
 * Set the network ID of the multiplex.
 * @param multiplex The multiplex to update.
 * @param netid The network id to set.
 * @return 0 on success, otherwise an SQLite error code.
 */
int MultiplexNetworkIdSet(Multiplex_t *multiplex, int netid);


/**
 * Create a new multiplex object.
 */
#define MultiplexNew() (Multiplex_t*)ObjectCreateType(Multiplex_t)

/**
 * Increment the references to the specified multiplex object.
 * @param __multiplex The multiplex instance to increment the reference count of. (Multiplex can be NULL.)
 */
#define MultiplexRefInc(__multiplex) \
        do{ \
            if ((__multiplex)) \
            { \
                ObjectRefInc((__multiplex)); \
            } \
        }while(0)

/**
 * Decrement the references of the specified multiplex object. If the reference 
 * count reaches 0 the instance is free'd.
 * @param __multiplex The multiplex instance to decrement the reference count of. (Multiplex can be NULL.)
 */
#define MultiplexRefDec(__multiplex) \
        do{ \
            if ((__multiplex)) \
            { \
                ObjectRefDec((__multiplex)); \
            } \
        }while(0)

/**
 * Use as the toString parameter when registering an event where the payload will
 * be a multiplex object.
 */
char *MultiplexEventToString(Event_t event,void * payload);
/** @} */
#endif
