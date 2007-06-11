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

dvb.h

Opens/Closes and setups dvb adapter for use in the rest of the application.

*/
#ifndef _DVB_H
#define _DVB_H
#include <stdint.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>
#include "types.h"

/**
 * @defgroup DVBAdapter DVB Adapter access
 * @{
 */
/**
 * Maximum number of PID filters when running in hardware restricted mode.
 */
#define DVB_MAX_PID_FILTERS 15

/**
 * Structure used to keep track of hardware pid filters.
 */
typedef struct DVBAdapterPIDFilter_s
{
    int demuxFd;  /**< File descriptor for the demux device. */
    uint16_t pid; /**< PID that is being filtered. */
    bool system;  /**< Whether this filter is for a 'system' PID */
}DVBAdapterPIDFilter_t;

/**
 * Structure representing a DVB Adapter, that is a frontend, a demux and a dvr
 * device.
 * Currently only supports the first frontend/demux/dvr per adapter.
 */
typedef struct DVBAdapter_t
{
    int adapter;                      /**< The adapter number ie /dev/dvb/adapter<#adapter> */
    struct dvb_frontend_info info;    /**< Information about the front end */

    char frontEndPath[30];            /**< Path to the frontend device */
    int frontEndFd;                   /**< File descriptor for the frontend device */

    char demuxPath[30];               /**< Path to the demux device */
    DVBAdapterPIDFilter_t filters[DVB_MAX_PID_FILTERS];/**< File descriptor for the demux device.*/

    char dvrPath[30];                 /**< Path to the dvr device */
    int dvrFd;                        /**< File descriptor for the dvr device */

    int lnbLowFreq;                   /**< LNB LO frequency information */
    int lnbHighFreq;                  /**< LNB LO frequency information */
    int lnbSwitchFreq;                /**< LNB LO frequency information */

    bool hardwareRestricted;          /**< Whether the adapter can only stream a
                                           portion of the transport stream */
}
DVBAdapter_t;

/**
 * Enum to represent the different polarisation available for satellite
 * transmission.
 */
enum Polarisation_e
{
    POL_HORIZONTAL = 0,
    POL_VERTICAL
};

/**
 * Structure used to hold the information necessary to setup DiSEqC switches 
 * to receive a specifiec satellite.
 */
typedef struct DVBDiSEqCSettings_s
{
    enum Polarisation_e polarisation;/**< Polarisation of the signal */
    unsigned long satellite_number;  /**< Satellite number for the switch */
}DVBDiSEqCSettings_t;
    
/**
 * Open a DVB Adapter.
 * This will open the frontend, demux and dvr devices.
 * @param adapter The adapter number of the devices to open.
 * @param hwRestricted Whether the adapter can only stream a portion of the 
 *                     transport stream.
 * @return a DVBAdapter_t structure or NULL if the adapter could not be opened.
 */
DVBAdapter_t *DVBInit(int adapter, bool hwRestricted);

/**
 * Close a DVPAdapter.
 * Close the frontend,demux and dvr devices and free the DVBAdapter_t structure.
 * @param adapter The DVBAdapter_t structure to free.
 */
void DVBDispose(DVBAdapter_t *adapter);

/**
 * Tune the frontend to the specified parameters.
 * @param adapter The adapter to tune.
 * @param frontend The parameters to use to tune.
 * @return 0 on success, non-zero otherwise.
 */
int DVBFrontEndTune(DVBAdapter_t *adapter, struct dvb_frontend_parameters *frontend, DVBDiSEqCSettings_t *diseqc);

/**
 * Set the LNB LO frequencies.
 * @param adapter The adapter to set the LNB information on.
 * @param lowFreq Low LO frequency.
 * @param highFreq high LO frequency.
 * @param switchFreq switch LO frequency.
 * @return 0 on success, non-zero otherwise.
 */
void DVBFrontEndLNBInfoSet(DVBAdapter_t *adapter, int lowFreq, int highFreq, int switchFreq);

/**
 * Retrieve the status of the frontend of the specified adapter.
 * @param adapter  The adapter to check.
 * @param status   Used to return the status flags (may be NULL).
 * @param ber      Used to return the Bit Error Rate (may be NULL).
 * @param strength Used to return the signal strength (may be NULL).
 * @param snr      Used to return the signal to noise ratio (may be NULL).
 * @param ucblocks Used to return the uncorrected block count (may be NULL).
 * @return 0 on success, non-zero otherwise.
 */
int DVBFrontEndStatus(DVBAdapter_t *adapter, fe_status_t *status, 
                            unsigned int *ber, unsigned int *strength, 
                            unsigned int *snr, unsigned int *ucblocks);

/**
 * Set the size of the circular buffer used by the demux.
 * @param adapter The adapter to set size of the buffer on.
 * @param size Size of the buffer to set.
 * @return 0 on success, non-zero otherwise.
 */
int DVBDemuxSetBufferSize(DVBAdapter_t *adapter, unsigned long size);

/**
 * Allocate a new PID Filter, indicating whether it is a system PID or not.
 * @param adapter The adapter to allocate the filter on,
 * @param pid The PID to filter.
 * @param system Whether this filter is a 'system' filter or not.
 * @return 0 on success, non-zero otherwise.
 */
int DVBDemuxAllocateFilter(DVBAdapter_t *adapter, uint16_t pid, bool system);

/**
 * Release a specific PID filter.
 * @param adapter The adapter to release the filter on.
 * @param pid The PID of the filter to release.
 * @return 0 on success, non-zero otherwise.
 */
int DVBDemuxReleaseFilter(DVBAdapter_t *adapter, uint16_t pid);

/**
 * Release all application or system PID filters.
 * @param adapter The adapter to release the filters on.
 * @param system Whether to release system or application filters.
 * @return 0 on success, non-zero otherwise.
 */ 
int DVBDemuxReleaseAllFilters(DVBAdapter_t *adapter, bool system);

/**
 * Read upto max bytes from the dvr device belonging to the specified adapter.
 * @param adapter The adapter to read from.
 * @param data Buffer to read into.
 * @param max Maximum number of bytes to read.
 * @param timeout Maximum amount of time to wait for data.
 * @return 0 on success, non-zero otherwise.
 */
int DVBDVRRead(DVBAdapter_t *adapter, char *data, int max, int timeout);


/** @} */
#endif
