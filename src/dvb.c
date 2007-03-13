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
 
dvb.c
 
Opens/Closes and setups dvb adapter for use in the rest of the application.
 
*/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <sys/ioctl.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>

#include "dvb.h"
#include "logging.h"

static int DVBDemuxSetPESFilter(DVBAdapter_t *adapter, ushort pid, int pidtype, int taptype);

DVBAdapter_t *DVBInit(int adapter)
{
    DVBAdapter_t *result = NULL;
    result = (DVBAdapter_t*)calloc(sizeof(DVBAdapter_t), 1);
    if (result)
    {
        result->frontEndFd = -1;
        result->demuxFd = -1;
        result->dvrFd = -1;
        result->adapter = adapter;
        sprintf(result->frontEndPath, "/dev/dvb/adapter%d/frontend0", adapter);
        sprintf(result->demuxPath, "/dev/dvb/adapter%d/demux0", adapter);
        sprintf(result->dvrPath, "/dev/dvb/adapter%d/dvr0", adapter);
        result->frontEndFd = open(result->frontEndPath, O_RDWR);
        if (result->frontEndFd == -1)
        {
            printlog(LOG_ERROR,"Failed to open %s : %s\n",result->frontEndPath, strerror(errno));
            DVBDispose(result);
            return NULL;
        }
        result->demuxFd = open(result->demuxPath, O_RDWR);
        if (result->demuxFd == -1)
        {
            printlog(LOG_ERROR,"Failed to open %s : %s\n",result->demuxPath, strerror(errno));
            DVBDispose(result);
            return NULL;
        }
        result->dvrFd = open(result->dvrPath, O_RDONLY | O_NONBLOCK);
        if (result->dvrFd == -1)
        {
            printlog(LOG_ERROR,"Failed to open %s : %s\n",result->dvrPath, strerror(errno));
            DVBDispose(result);
            return NULL;
        }

        /* Stream the entire TS to the DVR device */
        DVBDemuxSetPESFilter(result, 8192, DMX_PES_OTHER, DMX_OUT_TS_TAP);
    }
    return result;
}
void DVBDispose(DVBAdapter_t *adapter)
{
    if (adapter->dvrFd > -1)
    {
        printlog(LOG_DEBUGV,"Closing DVR file descriptor\n");
        close(adapter->dvrFd);
    }
    if (adapter->demuxFd > -1)
    {
        printlog(LOG_DEBUGV,"Closing Demux file descriptor\n");
        close(adapter->demuxFd);
    }
    if (adapter->frontEndFd > -1)
    {
        printlog(LOG_DEBUGV,"Closing Frontend file descriptor\n");
        close(adapter->frontEndFd);
    }
    free(adapter);
}

int DVBFrontEndTune(DVBAdapter_t *adapter, struct dvb_frontend_parameters *frontend)
{
#ifndef __CYGWIN__
    /*  fe_status_t festatus; */
    struct dvb_frontend_event event;
    struct pollfd pfd[1];

    if (ioctl(adapter->frontEndFd, FE_SET_FRONTEND, frontend) < 0)
    {
        printlog(LOG_ERROR,"setfront front: %s\n", strerror(errno));
        return 0;
    }

    pfd[0].fd = adapter->frontEndFd;
    pfd[0].events = POLLIN;

    if (poll(pfd,1,3000))
    {
        if (pfd[0].revents & POLLIN)
        {
            if (ioctl(adapter->frontEndFd, FE_GET_EVENT, &event) == -EOVERFLOW)
            {
                printlog(LOG_ERROR,"EOVERFLOW");
                return 0;
            }
            if (event.parameters.frequency <= 0)
            {
                return 0;
            }
        }
    }
#endif    
    return 1;
}

int DVBFrontEndStatus(DVBAdapter_t *adapter, fe_status_t *status, unsigned int *ber, unsigned int *strength, unsigned int *snr)
{
    #ifndef __CYGWIN__
    if (ioctl(adapter->frontEndFd, FE_READ_STATUS, status) < 0)
    {
        printlog(LOG_ERROR,"FE_READ_STATUS: %s\n", strerror(errno));
        return 0;
    }

    if(ioctl(adapter->frontEndFd,FE_READ_BER, ber) < 0)
    {
        printlog(LOG_ERROR,"FE_READ_BER: %s\n", strerror(errno));
        return 0;
    }

    if(ioctl(adapter->frontEndFd,FE_READ_SIGNAL_STRENGTH,strength) < 0)
    {
        printlog(LOG_ERROR,"FE_READ_SIGNAL_STRENGTH: %s\n", strerror(errno));
        return 0;
    }


    if(ioctl(adapter->frontEndFd,FE_READ_SNR,snr) < 0)
    {
        printlog(LOG_ERROR,"FE_READ_SNR: %s\n", strerror(errno));
        return 0;
    }
    #endif
    return 1;
}

static int DVBDemuxSetPESFilter(DVBAdapter_t *adapter, ushort pid, int pidtype, int taptype)
{
    struct dmx_pes_filter_params pesFilterParam;

    pesFilterParam.pid = pid;
    pesFilterParam.input = DMX_IN_FRONTEND;
    pesFilterParam.output = taptype;
    pesFilterParam.pes_type = pidtype;
    pesFilterParam.flags = DMX_IMMEDIATE_START;
    #ifndef __CYGWIN__
    if (ioctl(adapter->demuxFd, DMX_SET_PES_FILTER, &pesFilterParam) < 0)
    {
        printlog(LOG_ERROR,"set_pid: %s\n", strerror(errno));
        return 0;
    }
    #endif
    return 1;
}

int DVBDemuxSetBufferSize(DVBAdapter_t *adapter, unsigned long size)
{
    #ifndef __CYGWIN__
    if (ioctl(adapter->demuxFd, DMX_STOP, 0)< 0)
    {
        printlog(LOG_ERROR,"DMX_STOP: %s\n", strerror(errno));
        return 0;
    }
    if (ioctl(adapter->demuxFd, DMX_SET_BUFFER_SIZE, size) < 0)
    {
        printlog(LOG_ERROR,"DMX_SET_BUFFER_SIZE: %s\n", strerror(errno));
        return 0;
    }
    if (ioctl(adapter->demuxFd, DMX_START, 0)< 0)
    {
        printlog(LOG_ERROR,"DMX_STOP: %s\n", strerror(errno));
        return 0;
    }
    #endif
    return 1;
}

int DVBDVRRead(DVBAdapter_t *adapter, char *data, int max, int timeout)
{
    int result = -1;
    struct pollfd pfd[1];

    pfd[0].fd = adapter->dvrFd;
    pfd[0].events = POLLIN;

    if (poll(pfd,1,timeout))
    {
        if (pfd[0].revents & POLLIN)
        {
            result = read(adapter->dvrFd, data, max);
        }
    }
    return result;
}
