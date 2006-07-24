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
/**
 * @defgroup Multiplex Multiplex information
 * {@
 */

/**
 * Structure describing a multiplex.
 */
typedef struct Multiplex_t
{
    int freq;       /**< Frequency the multiplex is broadcast on. */
    int tsid;       /**< Transport Stream ID. */
    fe_type_t type; /**< The type of frontend used to receive this transport stream. */
    int patversion; /**< Last processed version of the PAT */
}
Multiplex_t;

typedef void * MultiplexEnumerator_t;

/**
 * Macro to compare 2 Multiplex_t structures.
 */
#define MultiplexAreEqual(_multiplex1, _multiplex2) \
    ((_multiplex1)->freq == (_multiplex2)->freq)

/**
 * Number of multiplexes stored in the database.
 * @return The number of multiplexes in the database.
 */
int MultiplexCount();
/**
 * Retrieve the Multiplex_t structure for the given frequency.
 * The returned structured should be free'd using free().
 * @param freq Frequency of the multiplex to retrieve.
 * @return A Mulitplex_t or NULL if the frequency could not be found.
 */
Multiplex_t *MultiplexFind(int freq);

/**
 * Retrieve an enumerator for all the multiplexes in the database.
 * @return An enumerator instance or NULL if there was not enough memory.
 */
MultiplexEnumerator_t MultiplexEnumeratorGet();

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
 * @return 0 on success, otherwise an SQLite error code.
 */
int MultiplexFrontendParametersGet(Multiplex_t *multiplex, struct dvb_frontend_parameters *feparams);

/**
 * Add a multiplex to the database.
 * @param type The type of frontend used to receive this transport stream.
 * @param feparams The parameters to pass to the frontend to tune to the new TS.
 * @return 0 on success, otherwise an SQLite error code.
 */
int MultiplexAdd(fe_type_t type, struct dvb_frontend_parameters *feparams);

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

/** @} */
#endif
