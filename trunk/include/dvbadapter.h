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

typedef enum DVBTuneDeliverySys_s {
    DELSYS_UNDEFINED,
    DELSYS_DVBC_ANNEX_AC,
    DELSYS_DVBC_ANNEX_B,
    DELSYS_DVBT,
    DELSYS_DSS,
    DELSYS_DVBS,
    DELSYS_DVBS2,
    DELSYS_DVBH,
    DELSYS_ISDBT,
    DELSYS_ISDBS,
    DELSYS_ISDBC,
    DELSYS_ATSC,
    DELSYS_ATSCMH,
    DELSYS_DMBTH,
    DELSYS_CMMB,
    DELSYS_DAB,
} DVBTuneDeliverySys_e; 

typedef enum DVBFrontEndType_e{
    FETYPE_DVBS,  /* only supports DVB-S delivery system*/
    FETYPE_DVBS2, /* supports DVB-S and DVB-S2 delivery systems */
    FETYPE_DVBT,  /* support DVB-T delivery system */
    FETYPE_DVBC,  /* supports DVB-C Annex AC/B delivery systems */
    FETYPE_ATSC,  /* supports ATSC delivery system */
}DVBFrontEndType_e;

/* These are the same as defined by v5 of the linuxdvb api */
#define DVBTUNEPROP_FREQ                3 /* used for the intermediate frequency when tuning satellite muxes */
#define DVBTUNEPROP_MODULATION          4
#define DVBTUNEPROP_BANDWIDTH_HZ        5
#define DVBTUNEPROP_INVERSION           6
#define DVBTUNEPROP_DISEQC_MASTER       7
#define DVBTUNEPROP_SYMBOL_RATE         8
#define DVBTUNEPROP_INNER_FEC           9
#define DVBTUNEPROP_VOLTAGE             10
#define DVBTUNEPROP_TONE                11
#define DVBTUNEPROP_PILOT               12
#define DVBTUNEPROP_ROLLOFF             13 

/* DVBStreamer special properties for satellites */
#define DVBTUNEPROP_POLARISATION        0x80000001
#define DVBTUNEPROP_SATELLITE_NUMBER    0x80000002

typedef struct DVBTuneProperty_s{
    uint32_t cmd;
    union {
        uint32_t data;
        struct {
            uint8_t data[32];
            uint32_t len;
        }buffer;
    }u;
}DVBTuneProperty_t;

typedef struct DVBTuneProperties_s{
    DVBTuneDeliverySys_e deliverySys;
    uint32_t frequency;
    uint32_t nrofProperties;
    DVBTuneProperty_t *properties;
}DVBTuneProperties_t;

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
 * Structure representing a DVB Adapter, that is a frontend, a demux and a dvr
 * device.
 * Currently only supports the first frontend/demux/dvr per adapter.
 */
typedef struct DVBAdapter_t
{
    int adapter;                      /**< The adapter number ie /dev/dvb/adapter<#adapter> */

    /* TODO Remove and replace with non-linuxdvb structure */
    struct dvb_frontend_info info;    /**< Information about the front end */

    char frontEndPath[30];            /**< Path to the frontend device */
    int frontEndFd;                   /**< File descriptor for the frontend device */
    bool frontEndLocked;              /**< Whether the frontend is currently locked onto a signal. */

    /* TODO Remove and replace with tuning properties */
    __u32 frontEndRequestedFreq;      /**< The frequency that the application requested, may be different from one used (ie DVB-S intermediate frequency) */
    struct dvb_frontend_parameters frontEndParams; /**< The current frontend configuration parameters. These may be updated when the frontend locks. */
    DVBDiSEqCSettings_t diseqcSettings; /**< Current DiSEqC settings for DVB-S */

    DVBTuneProperties_t tuningProperties;
    LNBInfo_t lnbInfo;                /**< LNB Information for DVB-S/S2 receivers */

    char demuxPath[30];               /**< Path to the demux device */
    bool hardwareRestricted;          /**< Whether the adapter can only stream a
                                           portion of the transport stream */
    int maxFilters;                   /**< Maximum number of available filters. */
    DVBAdapterPIDFilter_t filters[DVB_MAX_PID_FILTERS];/**< File descriptor for the demux device.*/

    char dvrPath[30];                 /**< Path to the dvr device */
    int dvrFd;                        /**< File descriptor for the dvr device */

    int cmdRecvFd;                    /**< File descriptor for monitor task to recieve commands */
    int cmdSendFd;                    /**< File descriptor to send commands to monitor task. */
    ev_io commandWatcher;
    ev_io frontendWatcher;
}
DVBAdapter_t;

    
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
 * @param diseqc DiSEqC settings, may be NULL if not a satellite frontend.
 * @return 0 on success, non-zero otherwise.
 */
int DVBFrontEndTune(DVBAdapter_t *adapter, struct dvb_frontend_parameters *frontend, DVBDiSEqCSettings_t *diseqc);

/**
 * Set the LNB LO frequencies.
 * @param adapter The adapter to set the LNB information on.
 * @param lnbInfo Pointer to an LNBInfo structure.
 * @return 0 on success, non-zero otherwise.
 */
void DVBFrontEndLNBInfoSet(DVBAdapter_t *adapter, LNBInfo_t *lnbInfo);

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
#define DVBDVRGetFD(adapter) (adapter)->dvrFd

/** @} */
#endif
