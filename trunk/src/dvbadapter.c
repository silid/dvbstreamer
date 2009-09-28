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

dvbadapter.c

Opens/Closes and setups dvb adapter for use in the rest of the application.

*/
#include <limits.h>
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
#include "dvbadapter.h"
#include "logging.h"
#include "objects.h"
#include "events.h"
#include "properties.h"


/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/
#define MONITOR_CMD_EXIT              0
#define MONITOR_CMD_RETUNING          1
#define MONITOR_CMD_FE_ACTIVE_CHANGED 2
/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static int DVBFrontEndSatelliteSetup(DVBAdapter_t *adapter, struct dvb_frontend_parameters *frontend, DVBDiSEqCSettings_t *diseqc);
static int DVBFrontEndDiSEqCSet(DVBAdapter_t *adapter, DVBDiSEqCSettings_t *diseqc, bool tone);
static int DVBDemuxStartFilter(DVBAdapter_t *adapter, DVBAdapterPIDFilter_t *filter);
static int DVBDemuxStopFilter(DVBAdapter_t *adapter, DVBAdapterPIDFilter_t *filter);
static void DVBDemuxStartAllFilters(DVBAdapter_t *adapter);
static void DVBDemuxStopAllFilters(DVBAdapter_t *adapter);

static void DVBFrontEndMonitorSend(DVBAdapter_t *adapter, char cmd);
static void *DVBFrontEndMonitor(void *arg);

static char *DVBEventToString(Event_t event, void *payload);

static int DVBPropertyNameGet(void *userArg, PropertyValue_t *value);
static int DVBPropertyActiveGet(void *userArg, PropertyValue_t *value);
static int DVBPropertyActiveSet(void *userArg, PropertyValue_t *value);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static const char DVBADAPTER[] = "DVBAdapter";
static const char propertyParent[] = "adapter";

static EventSource_t dvbSource = NULL;
static Event_t lockedEvent;
static Event_t unlockedEvent;
static Event_t tuningFailedEvent;
static Event_t feActiveEvent;
static Event_t feIdleEvent;

static char *FETypesStr[] = {
    "QPSK",
    "QAM",
    "OFDM",
    "ATSC"
};

static char *BroadcastSysemStr[] = {
    "DVB-S",
    "DVB-C",
    "DVB-T",
    "ATSC",
};

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
DVBAdapter_t *DVBInit(int adapter, bool hwRestricted)
{
    DVBAdapter_t *result = NULL;
    int monitorFds[2];

    if (dvbSource == NULL)
    {
        dvbSource = EventsRegisterSource("DVBAdapter");
        lockedEvent = EventsRegisterEvent(dvbSource, "Locked", DVBEventToString);
        unlockedEvent = EventsRegisterEvent(dvbSource, "Unlocked", DVBEventToString);
        tuningFailedEvent = EventsRegisterEvent(dvbSource, "TuneFailed", DVBEventToString);
        feActiveEvent  = EventsRegisterEvent(dvbSource, "FrontEndActive", DVBEventToString);
        feIdleEvent  = EventsRegisterEvent(dvbSource, "FrontEndIdle", DVBEventToString);
    }

    result = (DVBAdapter_t*)ObjectAlloc(sizeof(DVBAdapter_t));
    if (result)
    {
        int i;

        result->frontEndFd = -1;
        result->dvrFd = -1;
        result->adapter = adapter;
        sprintf(result->frontEndPath, "/dev/dvb/adapter%d/frontend0", adapter);
        sprintf(result->demuxPath, "/dev/dvb/adapter%d/demux0", adapter);
        sprintf(result->dvrPath, "/dev/dvb/adapter%d/dvr0", adapter);
        result->maxFilters = -1
        /* Determine max number of filters */
        for (i = 0; i < DVB_MAX_PID_FILTERS; i ++)
        {
            if (result->maxFilters == -1)
            {
                result->filters[i].demuxFd = open(result->demuxPath);
                if (result->filters[i] == -1)
                {
                    result->maxFilters = i;
                }
            }
            else
            {
                result->filters[i] = -1;
            }
            
        }

        for (i = 0; i < result->maxFilters; i ++)
        {
            close(result->filters[i]);
            result->filters[i] = -1;
        }

        result->frontEndFd = open(result->frontEndPath, O_RDWR);
        if (result->frontEndFd == -1)
        {
            LogModule(LOG_ERROR, DVBADAPTER, "Failed to open %s : %s\n",result->frontEndPath, strerror(errno));
            DVBDispose(result);
            return NULL;
        }

        if (ioctl(result->frontEndFd, FE_GET_INFO, &result->info) < 0)
        {
            LogModule(LOG_ERROR, DVBADAPTER, "Failed to get front end info: %s\n",strerror(errno));
            DVBDispose(result);
            return NULL;
        }

        result->dvrFd = open(result->dvrPath, O_RDONLY | O_NONBLOCK);
        if (result->dvrFd == -1)
        {
            LogModule(LOG_ERROR, DVBADAPTER, "Failed to open %s : %s\n",result->dvrPath, strerror(errno));
            DVBDispose(result);
            return NULL;
        }

        if (pipe(monitorFds) == -1)
        {
            LogModule(LOG_ERROR, DVBADAPTER, "Failed to create pipe : %s\n", strerror(errno));
            DVBDispose(result);
            return NULL;
        }

        result->monitorRecvFd = monitorFds[0];
        result->monitorSendFd = monitorFds[1];

        /* Attempt to detect if this is a PID filtering device which cannot supply the full TS */
        if (!hwRestricted)
        {
            FILE *fp;
            char sysPath[PATH_MAX];
            int speed;

            sprintf(sysPath, "/sys/class/dvb/dvb%d.demux0/device/speed", adapter);

            fp = fopen(sysPath, "r");
            if (fp)
            {
                if (fscanf(fp, "%d", &speed))
                {
                    LogModule(LOG_INFO, DVBADAPTER, "Bus speed = %d!\n",speed);
                    if (speed <= 12) /* USB 1.1 */
                    {
                        hwRestricted = TRUE;
                    }
                }
                fclose(fp);
            }
        }

        result->hardwareRestricted = hwRestricted;
        /* Stream the entire TS to the DVR device */
        if (hwRestricted)
        {
            LogModule(LOG_INFO, DVBADAPTER, "Running in hardware restricted mode!\n");
        }

        /* Start monitoring thread */
        pthread_create(&result->monitorThread, NULL, DVBFrontEndMonitor, result);
        LogRegisterThread(result->monitorThread, "AdapterMonitor");
        
        /* Add properties */
        PropertiesAddProperty(propertyParent, "number", "The number of the adapter being used",
            PropertyType_Int, &result->adapter, PropertiesSimplePropertyGet, NULL);
        PropertiesAddProperty(propertyParent, "name", "Hardware driver name",
            PropertyType_String, result, DVBPropertyNameGet, NULL);
        PropertiesAddProperty(propertyParent, "hwrestricted", "Whether the hardware is not capable of supplying the entire TS.",
            PropertyType_Boolean, &result->hardwareRestricted, PropertiesSimplePropertyGet, NULL);
        PropertiesAddProperty(propertyParent, "maxfilters", "The maximum number of PID filters available.",
            PropertyType_Boolean, &result->maxFilters, PropertiesSimplePropertyGet, NULL);
        PropertiesAddProperty(propertyParent, "type", "The type of broadcast the frontend is capable of receiving",
            PropertyType_String, &FETypesStr[result->info.type], PropertiesSimplePropertyGet, NULL);
        PropertiesAddProperty(propertyParent, "system", "The broadcast system the frontend is capable of receiving",
            PropertyType_String, &BroadcastSysemStr[result->info.type], PropertiesSimplePropertyGet, NULL);
        PropertiesAddProperty(propertyParent, "active","Whether the frontend is currently in use.",
            PropertyType_Boolean, result,DVBPropertyActiveGet,DVBPropertyActiveSet);
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
    DVBDemuxReleaseAllFilters(adapter);
    adapter->monitorExit = TRUE;

    if (adapter->frontEndFd > -1)
    {
        LogModule(LOG_DEBUGV, DVBADAPTER, "Closing Frontend file descriptor\n");
        close(adapter->frontEndFd);
        LogModule(LOG_DEBUGV, DVBADAPTER, "Closed Frontend file descriptor\n");
        adapter->frontEndFd = -1;
    }

    if (adapter->monitorThread)
    {
        DVBFrontEndMonitorSend(adapter, MONITOR_CMD_EXIT);
        pthread_join(adapter->monitorThread, NULL);
        close(adapter->monitorRecvFd);
        close(adapter->monitorSendFd);
    }
    PropertiesRemoveAllProperties(propertyParent);

    ObjectFree(adapter);
}

int DVBFrontEndTune(DVBAdapter_t *adapter, struct dvb_frontend_parameters *frontend, DVBDiSEqCSettings_t *diseqc)
{
    adapter->frontEndParams = *frontend;
    adapter->frontEndRequestedFreq = frontend->frequency;
    LogModule(LOG_DEBUG, DVBADAPTER, "Tuning to %d", frontend->frequency);
    if (adapter->info.type == FE_QPSK)
    {
        adapter->diseqcSettings = *diseqc;
        if (DVBFrontEndSatelliteSetup(adapter, &adapter->frontEndParams, diseqc))
        {
            return -1;
        }
    }
    DVBFrontEndMonitorSend(adapter, MONITOR_CMD_RETUNING);
    if (ioctl(adapter->frontEndFd, FE_SET_FRONTEND, &adapter->frontEndParams) < 0)
    {
        LogModule(LOG_ERROR, DVBADAPTER, "setfront front: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

void DVBFrontEndParametersGet(DVBAdapter_t *adapter, struct dvb_frontend_parameters *frontend, DVBDiSEqCSettings_t *diseqc)
{
    if ((adapter->info.type == FE_QPSK) && (diseqc != NULL))
    {
        memcpy(diseqc, &adapter->diseqcSettings, sizeof(DVBDiSEqCSettings_t));
    }

    if (frontend != NULL)
    {
        memcpy(frontend, &adapter->frontEndParams, sizeof(struct dvb_frontend_parameters));
        if (adapter->info.type == FE_QPSK)
        {
            /* This is so we don't return the intermediate frequency */
            frontend->frequency = adapter->frontEndRequestedFreq;
        }
    }
}

void DVBFrontEndLNBInfoSet(DVBAdapter_t *adapter, int lowFreq, int highFreq, int switchFreq)
{
    adapter->lnbLowFreq = lowFreq;
    adapter->lnbHighFreq = highFreq;
    adapter->lnbSwitchFreq = switchFreq;
}

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


int DVBFrontEndStatus(DVBAdapter_t *adapter, fe_status_t *status,
                            unsigned int *ber, unsigned int *strength,
                            unsigned int *snr, unsigned int *ucblock)
{
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
    return 0;
}

int DVBFrontEndSetActive(DVBAdapter_t *adapter, bool active)
{
    if (active && (adapter->frontEndFd == -1))
    {
        struct dvb_frontend_parameters feparams;
        DVBDiSEqCSettings_t diseqc;
        /* Open frontend */
        adapter->frontEndFd = open(adapter->frontEndPath, O_RDWR);
        if (adapter->frontEndFd == -1)
        {
            LogModule(LOG_ERROR, DVBADAPTER, "Failed to open %s : %s\n",adapter->frontEndPath, strerror(errno));
            return -1;
        }
        /* Signal monitor thread */
        DVBFrontEndMonitorSend(adapter, MONITOR_CMD_FE_ACTIVE_CHANGED);
        /* Fire frontend active event */
        EventsFireEventListeners(feActiveEvent, adapter);
        /* Retune */
        feparams = adapter->frontEndParams;
        if (adapter->info.type == FE_QPSK)
        {
            feparams.frequency = adapter->frontEndRequestedFreq;
            diseqc = adapter->diseqcSettings;
        }
        return DVBFrontEndTune(adapter, &feparams, &diseqc);
    }

    if (!active && (adapter->frontEndFd != -1))
    {
        /* Stop all filters */
        DVBDemuxStopAllFilters(adapter);
        /* Close frontend */
        close(adapter->frontEndFd);
        adapter->frontEndFd = -1;
        /* Signal monitor thread */
        DVBFrontEndMonitorSend(adapter, MONITOR_CMD_FE_ACTIVE_CHANGED);
        /* Fire frontend idle event */
        EventsFireEventListeners(feIdleEvent, adapter);
    }
    return 0;
}

int DVBDemuxSetBufferSize(DVBAdapter_t *adapter, unsigned long size)
{
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
    return 0;
}

int DVBDemuxAllocateFilter(DVBAdapter_t *adapter, uint16_t pid)
{
    int result = -1;
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
        LogModule(LOG_DEBUG, DVBADAPTER, "Allocation filter for pid 0x%x\n", pid);
        adapter->filters[idxToUse].demuxFd = open(adapter->demuxPath, O_RDWR);
        if (adapter->filters[idxToUse].demuxFd == -1)
        {
            LogModule(LOG_ERROR, DVBADAPTER, "Failed to open %s : %s when attempting to allocate filter for PID 0x%x\n", adapter->demuxPath, strerror(errno), pid);
        }
        else
        {
            struct dmx_pes_filter_params pesFilterParam;

            adapter->filters[idxToUse].pid = pid;

            pesFilterParam.pid = pid;
            pesFilterParam.input = DMX_IN_FRONTEND;
            pesFilterParam.output = DMX_OUT_TS_TAP;
            pesFilterParam.pes_type = DMX_PES_OTHER;
            if (adapter->frontEndLocked)
            {
                LogModule(LOG_DEBUG, DVBADAPTER, "Starting pid filter immediately!\n");
                pesFilterParam.flags = DMX_IMMEDIATE_START;
            }
            else
            {
                pesFilterParam.flags = 0;
            }

            if (ioctl(adapter->filters[idxToUse].demuxFd , DMX_SET_PES_FILTER, &pesFilterParam) < 0)
            {
                LogModule(LOG_ERROR, DVBADAPTER,"set_pid: %s\n", strerror(errno));
            }
            else
            {
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
                LogModule(LOG_DEBUG, DVBADAPTER, "Releasing filter for pid 0x%x\n",pid);
                close(adapter->filters[i].demuxFd);
                adapter->filters[i].demuxFd = -1;
                result = 0;
                break;
            }
        }
    }
    return result;
}

int DVBDemuxReleaseAllFilters(DVBAdapter_t *adapter)
{
    int result = -1;
    int i;
    LogModule(LOG_DEBUG, DVBADAPTER, "Releasing all filters");
    for (i = 0; i < DVB_MAX_PID_FILTERS; i ++)
    {
        if (adapter->filters[i].demuxFd != -1)
        {
            close(adapter->filters[i].demuxFd);
            adapter->filters[i].demuxFd = -1;
            result = 0;
        }
    }

    return result;
}

static int DVBDemuxStartFilter(DVBAdapter_t *adapter, DVBAdapterPIDFilter_t *filter)
{
    int result = 0;
    (void)adapter;

    LogModule(LOG_DEBUG, DVBADAPTER, "Starting filter %d\n", filter->pid);

    if (ioctl(filter->demuxFd , DMX_START, NULL) < 0)
    {
        LogModule(LOG_ERROR, DVBADAPTER,"filter start: %s\n", strerror(errno));
        result = -1;
    }

    return result;
}

static int DVBDemuxStopFilter(DVBAdapter_t *adapter, DVBAdapterPIDFilter_t *filter)
{
    int result = 0;
    (void)adapter;

    LogModule(LOG_DEBUG, DVBADAPTER, "Stopping filter %d\n", filter->pid);

    if (ioctl(filter->demuxFd , DMX_STOP, NULL) < 0)
    {
        LogModule(LOG_ERROR, DVBADAPTER,"filter start: %s\n", strerror(errno));
        result = -1;
    }

    return result;
}

static void DVBDemuxStartAllFilters(DVBAdapter_t *adapter)
{
    int i = 0;
    for (i = 0; i < DVB_MAX_PID_FILTERS; i ++)
    {
        if (adapter->filters[i].demuxFd != -1)
        {
            DVBDemuxStartFilter(adapter, &adapter->filters[i]);
        }
    }
}

static void DVBDemuxStopAllFilters(DVBAdapter_t *adapter)
{
    int i = 0;
    for (i = 0; i < DVB_MAX_PID_FILTERS; i ++)
    {
        if (adapter->filters[i].demuxFd != -1)
        {
            DVBDemuxStopFilter(adapter, &adapter->filters[i]);
        }
    }
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

static void DVBFrontEndMonitorSend(DVBAdapter_t *adapter, char cmd)
{
    if (write(adapter->monitorSendFd, &cmd, 1) != 1)
    {
        LogModule(LOG_ERROR, DVBADAPTER, "Failed to write to monitor pipe!");
    }
}

static void *DVBFrontEndMonitor(void *arg)
{
    DVBAdapter_t *adapter = arg;

    #define MAX_FDS 2
    fe_status_t status;
    struct pollfd pfd[MAX_FDS];
    int nrofPfds = 2;
    int i;
    bool feLocked = FALSE;

    pfd[0].fd = adapter->monitorRecvFd;
    pfd[0].events = POLLIN;

    pfd[1].fd = adapter->frontEndFd;
    pfd[1].events = POLLIN;

    /* Read initial status */
    if (ioctl(adapter->frontEndFd, FE_READ_STATUS, &status) == 0)
    {
        if (status & FE_HAS_LOCK)
        {
            adapter->frontEndLocked = TRUE;
        }
    }
    feLocked = adapter->frontEndLocked;

    while (!adapter->monitorExit)
    {
        int n = poll(pfd, nrofPfds, -1);
        if (n > 0)
        {
            for (i = 0; i < nrofPfds; i ++)
            {

                if ((pfd[i].revents & POLLIN) == 0)
                {
                    continue;
                }
                if (pfd[i].fd == adapter->frontEndFd)
                {

                    struct dvb_frontend_event event;
                    if (ioctl(adapter->frontEndFd, FE_GET_EVENT, &event) == 0)
                    {
                        if (event.status & FE_HAS_LOCK)
                        {
                            adapter->frontEndLocked = TRUE;
                        }
                        else
                        {
                            adapter->frontEndLocked = FALSE;
                        }


                        if (feLocked != adapter->frontEndLocked)
                        {
                            if (adapter->frontEndLocked)
                            {
                                DVBDemuxStartAllFilters(adapter);
                                adapter->frontEndParams = event.parameters;
                                EventsFireEventListeners(lockedEvent, adapter);

                            }
                            else
                            {
                                DVBDemuxStopAllFilters(adapter);
                                EventsFireEventListeners(unlockedEvent, adapter);
                            }
                            feLocked = adapter->frontEndLocked;
                        }

                        if (event.parameters.frequency <= 0)
                        {
                            EventsFireEventListeners(tuningFailedEvent, adapter);
                        }
                    }
                }

                if (pfd[i].fd == adapter->monitorRecvFd)
                {
                    char cmd;
                    if (read(adapter->monitorRecvFd, &cmd, 1) == 1)
                    {
                        switch (cmd)
                        {
                            case MONITOR_CMD_EXIT: /* Exit */
                                break;
                            case MONITOR_CMD_RETUNING:
                                DVBDemuxStopAllFilters(adapter);
                                break;
                            case MONITOR_CMD_FE_ACTIVE_CHANGED:
                                if (adapter->frontEndFd == -1)
                                {
                                    DVBDemuxStopAllFilters(adapter);
                                    nrofPfds = 1;
                                }
                                else
                                {
                                    nrofPfds = 2;
                                    pfd[1].fd = adapter->frontEndFd;
                                }
                                break;
                        }
                    }
                }
            }

        }
        else if (n == -1)
        {
            break;
        }
    }
    LogModule(LOG_DEBUG, DVBADAPTER, "Monitoring thread exited.\n");
    return NULL;
}

static char *DVBEventToString(Event_t event, void *payload)
{
    char *result = NULL;
    DVBAdapter_t *adapter = payload;
    if (asprintf(&result, "%d", adapter->adapter) == -1)
    {
        LogModule(LOG_ERROR, DVBADAPTER, "Failed to allocate memory for event description when converting event to string\n");
    }
    return result;
}

static int DVBPropertyNameGet(void *userArg, PropertyValue_t *value)
{
    DVBAdapter_t *adapter = userArg;
    value->u.string = strdup(adapter->info.name);
    return 0;
}

static int DVBPropertyActiveGet(void *userArg, PropertyValue_t *value)
{
    DVBAdapter_t *adapter = userArg;
    value->u.boolean = adapter->frontEndFd != -1;
    return 0;
}

static int DVBPropertyActiveSet(void *userArg, PropertyValue_t *value)
{
    DVBAdapter_t *adapter = userArg;
    return DVBFrontEndSetActive(adapter,value->u.boolean);
}

