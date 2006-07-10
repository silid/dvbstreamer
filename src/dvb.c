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

DVBAdapter_t *DVBInit(int adapter)
{
    DVBAdapter_t *result = NULL;
    result = (DVBAdapter_t*)calloc(sizeof(DVBAdapter_t), 1);
    if (result)
    {
        result->frontendfd = -1;
        result->demuxfd = -1;
        result->dvrfd = -1;
        result->adapter = adapter;
        sprintf(result->frontendPath, "/dev/dvb/adapter%d/frontend0", adapter);
        sprintf(result->demuxPath, "/dev/dvb/adapter%d/demux0", adapter);
        sprintf(result->dvrPath, "/dev/dvb/adapter%d/dvr0", adapter);
        result->frontendfd = open(result->frontendPath, O_RDWR);
        if (result->frontendfd == -1)
        {
            printlog(LOG_ERROR,"Failed to open %s : %s\n",result->frontendPath, strerror(errno));
            DVBDispose(result);
            return NULL;
        }
        result->demuxfd = open(result->demuxPath, O_RDWR);
        if (result->demuxfd == -1)
        {
            printlog(LOG_ERROR,"Failed to open %s : %s\n",result->demuxPath, strerror(errno));
            DVBDispose(result);
            return NULL;
        }
        result->dvrfd = open(result->dvrPath, O_RDONLY | O_NONBLOCK);
        if (result->dvrfd == -1)
        {
            printlog(LOG_ERROR,"Failed to open %s : %s\n",result->dvrPath, strerror(errno));
            DVBDispose(result);
            return NULL;
        }
    }
    return result;
}
void DVBDispose(DVBAdapter_t *adapter)
{
    if (adapter->dvrfd > -1)
    {
        printlog(LOG_DEBUGV,"Closing DVR file descriptor\n");
        close(adapter->dvrfd);
    }
    if (adapter->demuxfd > -1)
    {
        printlog(LOG_DEBUGV,"Closing Demux file descriptor\n");
        close(adapter->demuxfd);
    }
    if (adapter->frontendfd > -1)
    {
        printlog(LOG_DEBUGV,"Closing Frontend file descriptor\n");
        close(adapter->frontendfd);
    }
    free(adapter);
}

int DVBFrontEndTune(DVBAdapter_t *adapter, struct dvb_frontend_parameters *frontend)
{
    fe_status_t status = 0;
    /*  fe_status_t festatus; */
    struct dvb_frontend_event event;
    unsigned int strength;
    struct pollfd pfd[1];

    if (ioctl(adapter->frontendfd, FE_SET_FRONTEND, frontend) < 0)
    {
        printlog(LOG_ERROR,"setfront front: %s\n", strerror(errno));
        return 0;
    }

    pfd[0].fd = adapter->frontendfd;
    pfd[0].events = POLLIN;

    if (poll(pfd,1,3000))
    {
        if (pfd[0].revents & POLLIN)
        {
            if (ioctl(adapter->frontendfd, FE_GET_EVENT, &event) == -EOVERFLOW)
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
#if 0
    do
    {
        status = 0;
        if (ioctl(adapter->frontendfd, FE_READ_STATUS, &status) < 0)
        {
            printlog(LOG_ERROR,"fe get event: %s\n", strerror(errno));
            return 0;
        }

        printlog(LOG_DEBUGV,"status: %x\n", status);
        if (status & FE_HAS_LOCK)
        {
            break;
        }
        usleep(500000);
        printlog(LOG_DEBUGV,"Trying to get lock...");
    }
    while (!(status & FE_TIMEDOUT));

    /* inform the user of frontend status */
    printlog(LOG_INFO,"Tuner status:  %c%c%c%c%c%c\n",
             (status & FE_HAS_SIGNAL)?'S':' ',
             (status & FE_TIMEDOUT)?'T':' ',
             (status & FE_HAS_LOCK)?'L':' ',
             (status & FE_HAS_CARRIER)?'C':' ',
             (status & FE_HAS_VITERBI)?'V':' ',
             (status & FE_HAS_SYNC)?'s':' '
            );

    strength=0;
    if(ioctl(adapter->frontendfd,FE_READ_BER,&strength) >= 0)
        printlog(LOG_INFO," Bit error rate: %i\n",strength);

    strength=0;
    if(ioctl(adapter->frontendfd,FE_READ_SIGNAL_STRENGTH,&strength) >= 0)
        printlog(LOG_INFO," Signal strength: %i\n",strength);

    strength=0;
    if(ioctl(adapter->frontendfd,FE_READ_SNR,&strength) >= 0)
        printlog(LOG_INFO," Signal/Noise Ratio: %i\n",strength);

    if (status & FE_HAS_LOCK && !(status & FE_TIMEDOUT))
    {
        printlog(LOG_DEBUGV," Lock achieved at %lu Hz\n",(unsigned long)frontend->frequency);
        return 1;
    }
    else
    {
        printlog(LOG_DEBUGV,"Unable to achieve lock at %lu Hz\n",(unsigned long)frontend->frequency);
        return 0;
    }
#endif
    return 1;
}

int DVBFrontEndStatus(DVBAdapter_t *adapter, fe_status_t *status, unsigned int *ber, unsigned int *strength, unsigned int *snr)
{
    if (ioctl(adapter->frontendfd, FE_READ_STATUS, status) < 0)
    {
        printlog(LOG_ERROR,"FE_READ_STATUS: %s\n", strerror(errno));
        return 0;
    }

    if(ioctl(adapter->frontendfd,FE_READ_BER, ber) < 0)
    {
        printlog(LOG_ERROR,"FE_READ_BER: %s\n", strerror(errno));
        return 0;
    }

    if(ioctl(adapter->frontendfd,FE_READ_SIGNAL_STRENGTH,strength) < 0)
    {
        printlog(LOG_ERROR,"FE_READ_SIGNAL_STRENGTH: %s\n", strerror(errno));
        return 0;
    }


    if(ioctl(adapter->frontendfd,FE_READ_SNR,snr) < 0)
    {
        printlog(LOG_ERROR,"FE_READ_SNR: %s\n", strerror(errno));
        return 0;
    }
    return 1;
}

int DVBDemuxStreamEntireTSToDVR(DVBAdapter_t *adapter)
{
    return DVBDemuxSetPESFilter(adapter, 8192, DMX_PES_OTHER, DMX_OUT_TS_TAP);
}

int DVBDemuxSetPESFilter(DVBAdapter_t *adapter, ushort pid, int pidtype, int taptype)
{
    struct dmx_pes_filter_params pesFilterParam;

    pesFilterParam.pid = pid;
    pesFilterParam.input = DMX_IN_FRONTEND;
    pesFilterParam.output = taptype;
    pesFilterParam.pes_type = pidtype;
    pesFilterParam.flags = DMX_IMMEDIATE_START;
    if (ioctl(adapter->demuxfd, DMX_SET_PES_FILTER, &pesFilterParam) < 0)
    {
        printlog(LOG_ERROR,"set_pid: %s\n", strerror(errno));
        return 0;
    }
    return 1;
}

int DVBDVRRead(DVBAdapter_t *adapter, char *data, int max, int timeout)
{
    int result = -1;
    struct pollfd pfd[1];

    pfd[0].fd = adapter->dvrfd;
    pfd[0].events = POLLIN;

    if (poll(pfd,1,timeout))
    {
        if (pfd[0].revents & POLLIN)
        {
            result = read(adapter->dvrfd, data, max);
        }
    }
    return result;
}
