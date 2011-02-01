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
#include <pthread.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>
#include <sys/types.h>

#include <ev.h>
#include "lnb.h"
#include "types.h"

/**
 * @defgroup DVBAdapter DVB Adapter access
 * The dvb module provides access to the linuxdvb API via a simple adapter model
 * that encompasses the frontend/demux/dvr device into one object.
 * By default the entire transport stream is routed to the DVR device, although
 * for hardware restricted devices it is possible to allocate PID filters that 
 * are routed to the DVR device.
 * These PID filters are grouped into system and application filters, making it 
 * easier to release a specific set of PID filters that are being used to filter
 * a service say.
 *
 * \section events Events Exported
 *
 * \li \ref locked Sent when the frontend acquires a signal lock.
 * \li \ref unlocked Sent when the frontend loses signal lock.
 * \li \ref tuneFailed Sent when the frontend fails to tune.
 *
 * \subsection locked DVBAdapter.Locked
 * This event is fired from the DVBAdapter monitor thread when it detects that 
 * the frontend has acquired a signal lock. \n
 * \par 
 * \c payload = The adapter that has locked.
 *
 * \subsection unlocked DVBAdapter.Unlocked
 * This event is fired from the DVBAdapter monitor thread when it detects that 
 * the frontend has lost signal lock. \n
 * \par 
 * \c payload = The adapter that has lost locked.
 *
 * \subsection tuneFailed DVBAdapter.TuneFailed
 * This event is fired when the frontend fails to tune, for example parameters 
 * out of range etc. \n
 * \par
 * \c payload = The adapter that failed to tune.
 * @{
 */
/**
 * Maximum number of PID filters when running in hardware restricted mode.
 */
#define DVB_MAX_PID_FILTERS 256

/**
 * Structure used to keep track of hardware pid filters.
 */
typedef struct DVBAdapterPIDFilter_s
{
    int demuxFd;  /**< File descriptor for the demux device. */
    uint16_t pid; /**< PID that is being filtered. */
}DVBAdapterPIDFilter_t;

typedef enum DVBDeliverySystem_e {
    DELSYS_DVBS,
    DELSYS_DVBC,
    DELSYS_DVBT,
    DELSYS_ATSC,
    DELSYS_DVBS2,
    DELSYS_DVBT2,
    DELSYS_ISDBT,
    DELSYS_MAX_SUPPORTED
} DVBDeliverySystem_e; 

extern char *DVBDeliverySystemStr[];

typedef struct DVBSupportedDeliverySys_s
{
    int nrofSystems;
    DVBDeliverySystem_e systems[0];
}DVBSupportedDeliverySys_t;

typedef enum DVBFrontEndStatus_e {
    FESTATUS_HAS_SIGNAL = 0x01,     /**< Found something above the noise level */
    FESTATUS_HAS_CARRIER= 0x02,     /**< Found a DVB signal  */
    FESTATUS_HAS_VITERBI= 0x04,     /**< FEC is stable  */
    FESTATUS_HAS_SYNC   = 0x08,     /**< Found sync bytes  */
    FESTATUS_HAS_LOCK   = 0x10,     /**< Everything is working... */
    FESTATUS_TIMEDOUT   = 0x20,     /**< No lock within the last ~2 seconds */
    FESTATUS_REINIT     = 0x40      /**< Frontend was reinitialized */
} DVBFrontEndStatus_e;           

/**
 * Structure representing a DVB Adapter, that is a frontend, a demux and a dvr
 * device.
 * Currently only supports the first frontend/demux/dvr per adapter.
 */
typedef struct DVBAdapter_s DVBAdapter_t;

    
/**
 * Open a DVB Adapter.
 * This will open the frontend, demux and dvr devices.
 * @param adapter The adapter number of the devices to open.
 * @param hwRestricted Whether the adapter can only stream a portion of the 
 *                     transport stream.
 * @param forceISDB Force only ISDB to be supported.
 * @return a DVBAdapter_t structure or NULL if the adapter could not be opened.
 */
DVBAdapter_t *DVBInit(int adapter, bool hwRestricted, bool forceISDB);

/**
 * Close a DVPAdapter.
 * Close the frontend,demux and dvr devices and free the DVBAdapter_t structure.
 * @param adapter The DVBAdapter_t structure to free.
 */
void DVBDispose(DVBAdapter_t *adapter);

/**
 * Retieve the supported delivery systems for the specified DVB adapter.
 * @param adapter The adapter to retrieve the delivery systems supported from.
 * @return Pointer to a structure containing the supported delivery systems.
 */
DVBSupportedDeliverySys_t *DVBFrontEndGetDeliverySystems(DVBAdapter_t *adapter);

/**
 * Tune the frontend to the specified parameters.
 * @param adapter The adapter to tune.
 * @param system The delivery system to use.
 * @param params String containing the tuning paramaters in a yaml document.
 * @return 0 on success, non-zero otherwise.
 */
int DVBFrontEndTune(DVBAdapter_t *adapter, DVBDeliverySystem_e system, char *params);

/**
 * Retrieve the current tuning parameters.
 * @param adapter The adapter to get the parameters from.
 * @param system Pointer to store the current delivery system use in.
 * @return A string containing a YAML document, it is the callers job to free the
 * returned string.
 */
char* DVBFrontEndParametersGet(DVBAdapter_t *adapter, DVBDeliverySystem_e *system);

/**
 * Set the LNB LO frequencies.
 * @param adapter The adapter to set the LNB information on.
 * @param lnbInfo Pointer to an LNBInfo structure.
 */
void DVBFrontEndLNBInfoSet(DVBAdapter_t *adapter, LNBInfo_t *lnbInfo);

/**
 * Get the LNB LO frequencies.
 * @param adapter The adapter to get the LNB information on.
 * @param lnbInfo Pointer to an LNBInfo structure to store the LNB information in.
 */
void DVBFrontEndLNBInfoGet(DVBAdapter_t *adapter, LNBInfo_t *lnbInfo);

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
int DVBFrontEndStatus(DVBAdapter_t *adapter, DVBFrontEndStatus_e *status, 
                            unsigned int *ber, unsigned int *strength, 
                            unsigned int *snr, unsigned int *ucblocks);


/**
 * Query the adapter to determine if the frontend is locked.
 * @param adapter The adapter to query.
 * @return TRUE if the frontend is locked, FALSE otherwise.
 */
bool DVBFrontEndIsLocked(DVBAdapter_t *adapter);

/**
 * Check whether the frontend supports the parameter and value specified.
 * @param adapter The adapter to query.
 * @param system The delivery system to query.
 * @param param The name of the parameter to check.
 * @param value The value of the parameter to check.
 * @return TRUE if the paramater and value are supported for the delivery system and by the frontend.
 */
bool DVBFrontEndParameterSupported(DVBAdapter_t *adapter, DVBDeliverySystem_e system, char *param, char *value);

/**
 * Check whether the frontend supports the specified delivery system.
 * @param adapter The adapter to query.
 * @param system The delivery system to query.
 * @return TRUE if the delivery system is supported, FALSE otherwise.
 */
bool DVBFrontEndDeliverySystemSupported(DVBAdapter_t *adapter, DVBDeliverySystem_e system);

/**
 * Set the size of the circular buffer used by the demux.
 * @param adapter The adapter to set size of the buffer on.
 * @param size Size of the buffer to set.
 * @return 0 on success, non-zero otherwise.
 */
int DVBDemuxSetBufferSize(DVBAdapter_t *adapter, unsigned long size);

/**
 * Determine whether the demux is hardware restricted to a set number of 
 * filters and cannot return the full transport stream.
 * @param adapter The adapter to query.
 * @return TRUE if the demux is restricted, FALSE if the demux can return the whole TS.
 */
bool DVBDemuxIsHardwareRestricted(DVBAdapter_t *adapter);

/**
 * Get the maximum number of PID filters supported by the adapter.
 * @param adapter The adapter to get the number of filters from.
 * @return The maximum number of filters.
 */
int DVBDemuxGetMaxFilters(DVBAdapter_t *adapter);

/**
 * Get the number of available filters.
* @param adapter The adapter to get the number of filters from.
* @return The number of filters available.
*/
int DVBDemuxGetAvailableFilters(DVBAdapter_t *adapter);

/**
 * Allocate a new PID Filter, indicating whether it is a system PID or not.
 * @param adapter The adapter to allocate the filter on,
 * @param pid The PID to filter.
 * @return 0 on success, non-zero otherwise.
 */
int DVBDemuxAllocateFilter(DVBAdapter_t *adapter, uint16_t pid);

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
 * @return 0 on success, non-zero otherwise.
 */ 
int DVBDemuxReleaseAllFilters(DVBAdapter_t *adapter);

/**
 * Get the file descriptor for the DVR device to use in poll() etc.
 * @param adapter The adapter to get the DVR fd from. 
 */
int DVBDVRGetFD(DVBAdapter_t *adapter);

/** @} */
#endif
