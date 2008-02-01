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
#include <stdint.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <sys/ioctl.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>
#include <pthread.h>
#include "types.h"
#include "dvb.h"
#include "logging.h"
#include "objects.h"

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
#ifndef __CYGWIN__
static int DVBFrontEndSatelliteSetup(DVBAdapter_t *adapter, struct dvb_frontend_parameters *frontend, DVBDiSEqCSettings_t *diseqc);
static int DVBFrontEndDiSEqCSet(DVBAdapter_t *adapter, DVBDiSEqCSettings_t *diseqc, bool tone);
#endif

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static char DVBADAPTER[] = "DVBAdapter";

#ifdef __CYGWIN__
static volatile bool locked = FALSE;
static pthread_mutex_t tuningMutex;
#endif

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
DVBAdapter_t *DVBInit(int adapter, bool hwRestricted)
{
    DVBAdapter_t *result = NULL;
    result = (DVBAdapter_t*)ObjectAlloc(sizeof(DVBAdapter_t));
    if (result)
    {
        int i;
        /* Set all filters to be unallocated */
        for (i = 0; i < DVB_MAX_PID_FILTERS; i ++)
        {
            result->filters[i].demuxFd = -1;
        }
        
        result->frontEndFd = -1;
        result->dvrFd = -1;
        result->adapter = adapter;
        sprintf(result->frontEndPath, "/dev/dvb/adapter%d/frontend0", adapter);
        sprintf(result->demuxPath, "/dev/dvb/adapter%d/demux0", adapter);
        sprintf(result->dvrPath, "/dev/dvb/adapter%d/dvr0", adapter);
        result->frontEndFd = open(result->frontEndPath, O_RDWR);
        if (result->frontEndFd == -1)
        {
            LogModule(LOG_ERROR, DVBADAPTER, "Failed to open %s : %s\n",result->frontEndPath, strerror(errno));
            DVBDispose(result);
            return NULL;
        }
#ifndef __CYGWIN__        
        if (ioctl(result->frontEndFd, FE_GET_INFO, &result->info) < 0)
        {
            LogModule(LOG_ERROR, DVBADAPTER, "Failed to get front end info: %s\n",strerror(errno));
            DVBDispose(result);
            return NULL;
        }
#else
        if (adapter == 0)
        {
            result->info.type = FE_OFDM;
        }
        else
        {
            result->info.type = FE_ATSC;
        }
        strcpy(result->info.name, "Simulation");
#endif

#ifndef __CYGWIN__        
        result->dvrFd = open(result->dvrPath, O_RDONLY | O_NONBLOCK);
        if (result->dvrFd == -1)
        {
            LogModule(LOG_ERROR, DVBADAPTER, "Failed to open %s : %s\n",result->dvrPath, strerror(errno));
            DVBDispose(result);
            return NULL;
        }
#else
        result->dvrFd = -1;
#endif
        result->hardwareRestricted = hwRestricted;
        /* Stream the entire TS to the DVR device */
        if (!hwRestricted)
        {
            DVBDemuxAllocateFilter(result, 8192, TRUE);
        }
#ifdef __CYGWIN__     
        pthread_mutex_init(&tuningMutex, NULL);
#endif
    }
    return result;
}
void DVBDispose(DVBAdapter_t *adapter)
{
    if (adapter->dvrFd > -1)
    {
        LogModule(LOG_DEBUGV, DVBADAPTER, "Closing DVR file descriptor\n");
        close(adapter->dvrFd);
    }

    LogModule(LOG_DEBUGV, DVBADAPTER, "Closing Demux file descriptors\n");
    DVBDemuxReleaseAllFilters(adapter, FALSE);
    DVBDemuxReleaseAllFilters(adapter, TRUE);    
    
    if (adapter->frontEndFd > -1)
    {
        LogModule(LOG_DEBUGV, DVBADAPTER, "Closing Frontend file descriptor\n");
        close(adapter->frontEndFd);
    }
#ifdef __CYGWIN__         
    pthread_mutex_destroy(&tuningMutex);
#endif
    ObjectFree(adapter);
}

int DVBFrontEndTune(DVBAdapter_t *adapter, struct dvb_frontend_parameters *frontend, DVBDiSEqCSettings_t *diseqc)
{
#ifndef __CYGWIN__
    /*  fe_status_t festatus; */
    struct dvb_frontend_parameters localFEParams = *frontend;
    struct dvb_frontend_event event;
    struct pollfd pfd[1];

    if (adapter->info.type == FE_QPSK)
    {
        if (DVBFrontEndSatelliteSetup(adapter, &localFEParams, diseqc))
        {
            return -1;
        }
    }
    
    if (ioctl(adapter->frontEndFd, FE_SET_FRONTEND, &localFEParams) < 0)
    {
        LogModule(LOG_ERROR, DVBADAPTER, "setfront front: %s\n", strerror(errno));
        return -1;
    }

    pfd[0].fd = adapter->frontEndFd;
    pfd[0].events = POLLIN;

    if (poll(pfd,1,3000))
    {
        if (pfd[0].revents & POLLIN)
        {
            if (ioctl(adapter->frontEndFd, FE_GET_EVENT, &event) == -EOVERFLOW)
            {
                LogModule(LOG_ERROR, DVBADAPTER,"EOVERFLOW");
                return -1;
            }
            if (event.parameters.frequency <= 0)
            {
                return -1;
            }
        }
    }
#else
    char filename[256];
    sprintf(filename, "/dev/dvb/adapter%d/%d", adapter->adapter, frontend->frequency);
    pthread_mutex_lock(&tuningMutex);
    LogModule(LOG_DEBUG, DVBADAPTER, "Attempting to open %s\n", filename);
    if (adapter->dvrFd> -1)
    {
        close(adapter->dvrFd);
    }
    adapter->dvrFd = open(filename, O_RDONLY | O_NONBLOCK);
    locked = (adapter->dvrFd != -1) ? TRUE:FALSE;
    pthread_mutex_unlock(&tuningMutex);
#endif       
    return 0;
}

void DVBFrontEndLNBInfoSet(DVBAdapter_t *adapter, int lowFreq, int highFreq, int switchFreq)
{
    adapter->lnbLowFreq = lowFreq;
    adapter->lnbHighFreq = highFreq;
    adapter->lnbSwitchFreq = switchFreq;
}
#ifndef  __CYGWIN__    
static int DVBFrontEndSatelliteSetup(DVBAdapter_t *adapter, struct dvb_frontend_parameters *frontend, DVBDiSEqCSettings_t *diseqc)
{
    int hiband = 0;
    int ifreq = 0;
    bool tone = FALSE;
    
    if (adapter->lnbSwitchFreq && adapter->lnbHighFreq && 
        (frontend->frequency >= adapter->lnbSwitchFreq))
    {
        hiband = 1;
    }

    if (hiband)
    {
      ifreq = frontend->frequency - adapter->lnbHighFreq ;
      tone = TRUE;
    }
    else 
    {
      if (frontend->frequency < adapter->lnbLowFreq)
      {
          ifreq = adapter->lnbLowFreq - frontend->frequency;
      }
      else
      {
          ifreq = frontend->frequency - adapter->lnbLowFreq;
      }
      tone = FALSE;
    }

    frontend->frequency = ifreq;
    
    return DVBFrontEndDiSEqCSet(adapter, diseqc, tone);
}

static int DVBFrontEndDiSEqCSet(DVBAdapter_t *adapter, DVBDiSEqCSettings_t *diseqc, bool tone)
{
   struct dvb_diseqc_master_cmd cmd =
      {{0xe0, 0x10, 0x38, 0xf0, 0x00, 0x00}, 4};

   cmd.msg[3] = 0xf0 | ((diseqc->satellite_number* 4) & 0x0f) |
      (tone ? 1 : 0) | (diseqc->polarisation? 0 : 2);

   if (ioctl(adapter->frontEndFd, FE_SET_TONE, SEC_TONE_OFF) < 0)
   {
      return -1;
   }
   
   if (ioctl(adapter->frontEndFd, FE_SET_VOLTAGE, diseqc->polarisation ? SEC_VOLTAGE_13 : SEC_VOLTAGE_18) < 0)
   {
      return -1;
   }
   usleep(15000);
   
   if (ioctl(adapter->frontEndFd, FE_DISEQC_SEND_MASTER_CMD, &cmd) < 0)
   {
      return -1;
   }
   usleep(15000);

   if (ioctl(adapter->frontEndFd, FE_DISEQC_SEND_BURST, diseqc->satellite_number % 2 ? SEC_MINI_B : SEC_MINI_A) < 0)
   {
      return -1;
   }
   usleep(15000);
   if (ioctl(adapter->frontEndFd, FE_SET_TONE, tone ? SEC_TONE_ON : SEC_TONE_OFF) < 0)
   {
      return -1;
   }
   return 0;
}
#endif   


int DVBFrontEndStatus(DVBAdapter_t *adapter, fe_status_t *status, 
                            unsigned int *ber, unsigned int *strength,
                            unsigned int *snr, unsigned int *ucblock)
{
    #ifndef __CYGWIN__
    uint32_t tempU32;
    uint16_t tempU16;

    if (status)
    {
        if (ioctl(adapter->frontEndFd, FE_READ_STATUS, status) < 0)
        {
            LogModule(LOG_ERROR, DVBADAPTER,"FE_READ_STATUS: %s\n", strerror(errno));
            return -1;
        }
    }
    
    if (ber)
    {
        if(ioctl(adapter->frontEndFd,FE_READ_BER, &tempU32) < 0)
        {
            LogModule(LOG_INFO, DVBADAPTER,"FE_READ_BER: %s\n", strerror(errno));
            *ber = 0xffffffff;
        }
        else
        {
            *ber = tempU32;
        }
    }
    if (strength)
    {
        if(ioctl(adapter->frontEndFd,FE_READ_SIGNAL_STRENGTH,&tempU16) < 0)
        {
            LogModule(LOG_INFO, DVBADAPTER,"FE_READ_SIGNAL_STRENGTH: %s\n", strerror(errno));
            *strength = 0xffff;
        }
        else
        {
            *strength = tempU16;
        }
    }

    if (snr)
    {
        if(ioctl(adapter->frontEndFd,FE_READ_SNR,&tempU16) < 0)
        {
            LogModule(LOG_INFO, DVBADAPTER,"FE_READ_SNR: %s\n", strerror(errno));
            *snr = 0xffff;
        }
        else
        {
            *snr = tempU16;
        }
    }
    if (ucblock)
    {
        if(ioctl(adapter->frontEndFd,FE_READ_UNCORRECTED_BLOCKS,&tempU32) < 0)
        {
            LogModule(LOG_INFO, DVBADAPTER,"FE_READ_UNCORRECTED_BLOCKS: %s\n", strerror(errno));
            *ucblock = 0xffffffff;
        }
        else        
        {
            *ucblock = tempU32;
        }
    }
    #else
    if (status)
    {
        if (locked)
        {
            *status = FE_HAS_LOCK;
        }
        else
        {
            *status = 0;
        }
    }
    if (ber)
    {
        *ber = 0xffffffff;
    }
    if (strength)
    {
        *strength = 0xffff;
    }
    if (snr)
    {
        *snr = 0xffff;
    }
    if (ucblock)
    {
        *ucblock = 0xffffffff;
    }
    #endif
    return 0;
}

int DVBDemuxSetBufferSize(DVBAdapter_t *adapter, unsigned long size)
{
    #ifndef __CYGWIN__
    int i;
    for (i = 0; i < DVB_MAX_PID_FILTERS; i++)
    {
        int demuxFd = adapter->filters[0].demuxFd;
        
        if (demuxFd == -1)
        {
            continue;
        }
        
        if (ioctl(demuxFd, DMX_STOP, 0)< 0)
        {
            LogModule(LOG_ERROR, DVBADAPTER,"DMX_STOP: %s\n", strerror(errno));
            return -1;
        }
        if (ioctl(demuxFd, DMX_SET_BUFFER_SIZE, size) < 0)
        {
            LogModule(LOG_ERROR, DVBADAPTER,"DMX_SET_BUFFER_SIZE: %s\n", strerror(errno));
            return -1;
        }
        if (ioctl(demuxFd, DMX_START, 0)< 0)
        {
            LogModule(LOG_ERROR, DVBADAPTER,"DMX_STOP: %s\n", strerror(errno));
            return -1;
        }
    }
    #endif
    return 0;
}

int DVBDemuxAllocateFilter(DVBAdapter_t *adapter, uint16_t pid, bool system)
{
    int result = -1;
    if (adapter->hardwareRestricted || (pid == 8192))
    {
        int i;
        int idxToUse = -1;

        for (i = 0; i < DVB_MAX_PID_FILTERS; i ++)
        {
            if (adapter->filters[i].demuxFd == -1)
            {
                idxToUse = i;
            }
            else
            {
                if (adapter->filters[i].pid == pid)
                {
                    /* Already streaming this PID */
                    idxToUse = -1;
                    result = 0;
                    break;
                }
            }
        }
        if (idxToUse != -1)
        {
            LogModule(LOG_DEBUG, DVBADAPTER, "Allocation filter for pid 0x%x type %s\n", pid, system ? "System":"Service");
            adapter->filters[idxToUse].demuxFd = open(adapter->demuxPath, O_RDWR);
            if (adapter->filters[idxToUse].demuxFd == -1)
            {
                LogModule(LOG_ERROR, DVBADAPTER, "Failed to open %s : %s when attempting to allocate filter for PID 0x%x\n", adapter->demuxPath, strerror(errno), pid);
            }
            else
            {
                struct dmx_pes_filter_params pesFilterParam;

                adapter->filters[idxToUse].pid = pid;
                adapter->filters[idxToUse].system = system;

                pesFilterParam.pid = pid;
                pesFilterParam.input = DMX_IN_FRONTEND;
                pesFilterParam.output = DMX_OUT_TS_TAP;
                pesFilterParam.pes_type = DMX_PES_OTHER;
                pesFilterParam.flags = DMX_IMMEDIATE_START;
#ifndef __CYGWIN__
                if (ioctl(adapter->filters[idxToUse].demuxFd , DMX_SET_PES_FILTER, &pesFilterParam) < 0)
                {
                    LogModule(LOG_ERROR, DVBADAPTER,"set_pid: %s\n", strerror(errno));
                    return 0;
                }
#endif
                            
                result = 0;
            }
        }
    }
    return result;
}

int DVBDemuxReleaseFilter(DVBAdapter_t *adapter, uint16_t pid)
{
    int result = -1;
    if (adapter->hardwareRestricted || (pid == 8192))
    {
        int i;
        for (i = 0; i < DVB_MAX_PID_FILTERS; i ++)
        {
            if ((adapter->filters[i].demuxFd != -1) && (adapter->filters[i].pid == pid))
            {
                LogModule(LOG_DEBUG, DVBADAPTER, "Releasing filter for pid 0x%x type %s\n", 
                    pid, adapter->filters[i].system ? "System":"Service");
                close(adapter->filters[i].demuxFd);
                adapter->filters[i].demuxFd = -1;
                result = 0;
                break;
            }
        }
    }
    return result;
}

int DVBDemuxReleaseAllFilters(DVBAdapter_t *adapter, bool system)
{
    int result = -1;
    int i;
    LogModule(LOG_DEBUG, DVBADAPTER, "Releasing all filters for type %s\n",
        system ? "System":"Service");
    for (i = 0; i < DVB_MAX_PID_FILTERS; i ++)
    {
        if ((adapter->filters[i].demuxFd != -1) && (adapter->filters[i].system == system))
        {
            close(adapter->filters[i].demuxFd);
            adapter->filters[i].demuxFd = -1;
            result = 0;
        }
    }

    return result;
}

int DVBDVRRead(DVBAdapter_t *adapter, char *data, int max, int timeout)
{
#ifndef __CYGWIN__    
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
#else    
    int result = -1;    
    static unsigned int count = 0;
    pthread_mutex_lock(&tuningMutex);    
    if (locked)
    {
        tryagain:
        result = read(adapter->dvrFd, data, max);
        if (result <= 0)
        {
            lseek(adapter->dvrFd,0,0);
            goto tryagain;
        }

        if (result > -1)
        {
            count += result / 188;
        }

        if (count > 200)
        {
            usleep(100);
            count = 0;
        }
        
    }
    else
    {
        usleep(timeout * 10);
    }
    pthread_mutex_unlock(&tuningMutex);
    #endif
    return result;
}
