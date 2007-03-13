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
#include <sys/types.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>

/**
 * @defgroup DVBAdapter DVB Adapter access
 * @{
 */

/**
 * Structure representing a DVB Adapter, that is a frontend, a demux and a dvr
 * device.
 * Currently only supports the first frontend/demux/dvr per adapter.
 */
typedef struct DVBAdapter_t
{
    int adapter;           /**< The adapter number ie /dev/dvb/adapter<#adapter> */
    // /dev/dvb/adapter#/frontend0
    char frontEndPath[30]; /**< Path to the frontend device */
    int frontEndFd;        /**< File descriptor for the frontend device */
    // /dev/dvb/adapter#/demux0
    char demuxPath[30];    /**< Path to the demux device */
    int demuxFd;           /**< File descriptor for the demux device */
    // /dev/dvb/adapter#/dvr0
    char dvrPath[30];      /**< Path to the dvr device */
    int dvrFd;             /**< File descriptor for the dvr device */
}
DVBAdapter_t;

/**
 * Open a DVB Adapter.
 * This will open the frontend, demux and dvr devices.
 * @param adapter The adapter number of the devices to open.
 * @return a DVBAdapter_t structure or NULL if the adapter could not be opened.
 */
DVBAdapter_t *DVBInit(int adapter);

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
int DVBFrontEndTune(DVBAdapter_t *adapter, struct dvb_frontend_parameters *frontend);

/**
 * Retrieve the status of the frontend of the specified adapter.
 * @param adapter  The adapter to check.
 * @param status   Used to return the status flags.
 * @param ber      Used to return the Bit Error Rate.
 * @param strength Used to return the signal strength.
 * @param snr      Used to return the signal to noise ratio.
 * @return 0 on success, non-zero otherwise.
 */
int DVBFrontEndStatus(DVBAdapter_t *adapter, fe_status_t *status, unsigned int *ber, unsigned int *strength, unsigned int *snr);

/**
 * Set the size of the circular buffer used by the demux.
 * @param adapter The adapter to set size of the buffer on.
 * @param size Size of the buffer to set.
 * @return 0 on success, non-zero otherwise.
 */
int DVBDemuxSetBufferSize(DVBAdapter_t *adapter, unsigned long size);

/**
 * Read upto max bytes from the dvr device belonging to the specified adapter.
 * @param adapter The adapter to read from.
 * @param
 * @return 0 on success, non-zero otherwise.
 */
int DVBDVRRead(DVBAdapter_t *adapter, char *data, int max, int timeout);

/** @} */
#endif
