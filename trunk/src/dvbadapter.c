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
#include "dispatchers.h"

/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/
#define DVB_CMD_TUNE              0
#define DVB_CMD_FE_ACTIVE_TOGGLE  1

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static int DVBFrontEndSatelliteSetup(DVBAdapter_t *adapter, struct dvb_frontend_parameters *frontend, DVBDiSEqCSettings_t *diseqc);
static int DVBFrontEndDiSEqCSet(DVBAdapter_t *adapter, DVBDiSEqCSettings_t *diseqc, bool tone);
static int DVBDemuxStartFilter(DVBAdapter_t *adapter, DVBAdapterPIDFilter_t *filter);
static int DVBDemuxStopFilter(DVBAdapter_t *adapter, DVBAdapterPIDFilter_t *filter);
static void DVBDemuxStartAllFilters(DVBAdapter_t *adapter);
static void DVBDemuxStopAllFilters(DVBAdapter_t *adapter);

static void DVBCommandCallback(struct ev_loop *loop, ev_io *w, int revents);
static void DVBFrontendCallback(struct ev_loop *loop, ev_io *w, int revents);

static void DVBCommandSend(DVBAdapter_t *adapter, char cmd);


static char *DVBEventToString(Event_t event, void *payload);

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
    struct ev_loop *inputLoop;

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
        result->maxFilters = DVB_MAX_PID_FILTERS;
        /* Determine max number of filters */
        for (i = 0; i < DVB_MAX_PID_FILTERS; i ++)
        {
            if (result->maxFilters > i)
            {
                result->filters[i].demuxFd = open(result->demuxPath, O_RDWR);
                if (result->filters[i].demuxFd == -1)
                {
                    result->maxFilters = i;
                }
            }
            else
            {
                result->filters[i].demuxFd = -1;
            }
            
        }

        LogModule(LOG_INFO, DVBADAPTER, "Maximum filters = %d", result->maxFilters);
        for (i = 0; i < result->maxFilters; i ++)
        {
            close(result->filters[i].demuxFd);
            result->filters[i].demuxFd = -1;
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

        result->cmdRecvFd = monitorFds[0];
        result->cmdSendFd = monitorFds[1];

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
        inputLoop = DispatchersGetInput();
        ev_io_init(&result->frontendWatcher, DVBFrontendCallback, result->frontEndFd, EV_READ);
        ev_io_init(&result->commandWatcher, DVBCommandCallback, result->cmdRecvFd, EV_READ);
        result->frontendWatcher.data = result;
        result->commandWatcher.data = result;
        ev_io_start(inputLoop, &result->frontendWatcher);
        ev_io_start(inputLoop, &result->commandWatcher);        
        
        /* Add properties */
        PropertiesAddSimpleProperty(propertyParent, "number", "The number of the adapter being used",
            PropertyType_Int, &result->adapter, SIMPLEPROPERTY_R);
        PropertiesAddSimpleProperty(propertyParent, "name", "Hardware driver name",
            PropertyType_String, result->info.name, SIMPLEPROPERTY_R);
        PropertiesAddSimpleProperty(propertyParent, "hwrestricted", "Whether the hardware is not capable of supplying the entire TS.",
            PropertyType_Boolean, &result->hardwareRestricted, SIMPLEPROPERTY_R);
        PropertiesAddSimpleProperty(propertyParent, "maxfilters", "The maximum number of PID filters available.",
            PropertyType_Boolean, &result->maxFilters, SIMPLEPROPERTY_R);
        PropertiesAddSimpleProperty(propertyParent, "type", "The type of broadcast the frontend is capable of receiving",
            PropertyType_String, &FETypesStr[result->info.type], SIMPLEPROPERTY_R);
        PropertiesAddSimpleProperty(propertyParent, "system", "The broadcast system the frontend is capable of receiving",
            PropertyType_String, &BroadcastSysemStr[result->info.type], SIMPLEPROPERTY_R);
        PropertiesAddProperty(propertyParent, "active","Whether the frontend is currently in use.",
            PropertyType_Boolean, result,DVBPropertyActiveGet,DVBPropertyActiveSet);
    }
    return result;
}

void DVBDispose(DVBAdapter_t *adapter)
{
    struct ev_loop *inputLoop = DispatchersGetInput();
    if (adapter->dvrFd > -1)
    {
        LogModule(LOG_DEBUGV, DVBADAPTER, "Closing DVR file descriptor\n");
        close(adapter->dvrFd);
    }

    LogModule(LOG_DEBUGV, DVBADAPTER, "Closing Demux file descriptors\n");
    DVBDemuxReleaseAllFilters(adapter);
    ev_io_stop(inputLoop, &adapter->frontendWatcher);
    ev_io_stop(inputLoop, &adapter->commandWatcher);

    if (adapter->frontEndFd > -1)
    {
        LogModule(LOG_DEBUGV, DVBADAPTER, "Closing Frontend file descriptor\n");
        close(adapter->frontEndFd);
        LogModule(LOG_DEBUGV, DVBADAPTER, "Closed Frontend file descriptor\n");
        adapter->frontEndFd = -1;
    }
    
    close(adapter->cmdRecvFd);
    close(adapter->cmdSendFd);

    PropertiesRemoveAllProperties(propertyParent);

    ObjectFree(adapter);
}

int DVBFrontEndTune(DVBAdapter_t *adapter, struct dvb_frontend_parameters *frontend, DVBDiSEqCSettings_t *diseqc)
{
    adapter->frontEndParams = *frontend;
    adapter->frontEndRequestedFreq = frontend->frequency;
    if (diseqc)
    {
        adapter->diseqcSettings = *diseqc;
    }
    DVBCommandSend(adapter, DVB_CMD_TUNE);
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

void DVBFrontEndLNBInfoSet(DVBAdapter_t *adapter, LNBInfo_t *lnbInfo)
{
    adapter->lnbInfo = *lnbInfo;
}

static int DVBFrontEndSatelliteSetup(DVBAdapter_t *adapter, struct dvb_frontend_parameters *frontend, DVBDiSEqCSettings_t *diseqc)
{
    bool tone = FALSE;

    frontend->frequency = LNBTransponderToIntermediateFreq(&adapter->lnbInfo, frontend->frequency, &tone);

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


int DVBFrontEndStatus(DVBAdapter_t *adapter, DVBFrontEndStatus_e *status,
                            unsigned int *ber, unsigned int *strength,
                            unsigned int *snr, unsigned int *ucblock)
{
    uint32_t tempU32;
    uint16_t tempU16;

    if (status)
    {
        if (ioctl(adapter->frontEndFd, FE_READ_STATUS, (fe_status_t*)status) < 0)
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
    if ((active && (adapter->frontEndFd != -1)) ||
        (!active && (adapter->frontEndFd == -1)))
    {
        /* No change in active state */
        return 0;
    }
    DVBCommandSend(adapter, DVB_CMD_FE_ACTIVE_TOGGLE);
    return 0;
}

int DVBDemuxSetBufferSize(DVBAdapter_t *adapter, unsigned long size)
{
    int i;
    for (i = 0; i < adapter->maxFilters; i++)
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

    for (i = 0; i < adapter->maxFilters; i ++)
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

            LogModule(LOG_DEBUG, DVBADAPTER, "Filter fd %d\n", adapter->filters[idxToUse].demuxFd);

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
        for (i = 0; i < adapter->maxFilters; i ++)
        {
            if ((adapter->filters[i].demuxFd != -1) && (adapter->filters[i].pid == pid))
            {
                LogModule(LOG_DEBUG, DVBADAPTER, "Releasing filter for pid 0x%x fd %d\n",pid, adapter->filters[i].demuxFd);
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
    for (i = 0; i < adapter->maxFilters; i ++)
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
        LogModule(LOG_ERROR, DVBADAPTER,"filter(fd %d) start: %s\n", filter->demuxFd, strerror(errno));
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
        LogModule(LOG_ERROR, DVBADAPTER,"filter(fd %d) stop: %s\n", filter->demuxFd, strerror(errno));
        result = -1;
    }

    return result;
}

static void DVBDemuxStartAllFilters(DVBAdapter_t *adapter)
{
    int i = 0;
    for (i = 0; i < adapter->maxFilters; i ++)
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
    for (i = 0; i < adapter->maxFilters; i ++)
    {
        if (adapter->filters[i].demuxFd != -1)
        {
            DVBDemuxStopFilter(adapter, &adapter->filters[i]);
        }
    }
}

static void DVBCommandSend(DVBAdapter_t *adapter, char cmd)
{
    if (write(adapter->cmdSendFd, &cmd, 1) != 1)
    {
        LogModule(LOG_ERROR, DVBADAPTER, "Failed to write to monitor pipe!");
    }
}

static void DVBCommandCallback(struct ev_loop *loop, ev_io *w, int revents)
{
    DVBAdapter_t *adapter = w->data;
    char cmd;
    bool retune = FALSE;
    ev_io_start(loop, w);
    if (read(adapter->cmdRecvFd, &cmd, 1) == 1)
    {
        switch (cmd)
        {
            case DVB_CMD_TUNE:
                DVBDemuxStopAllFilters(adapter);
                retune = TRUE;
                break;
                
            case DVB_CMD_FE_ACTIVE_TOGGLE:
                if (adapter->frontEndFd == -1)
                {
                    retune = TRUE;
                    /* Open frontend */
                    adapter->frontEndFd = open(adapter->frontEndPath, O_RDWR);
                    if (adapter->frontEndFd == -1)
                    {
                        LogModule(LOG_ERROR, DVBADAPTER, "Failed to open %s : %s\n", adapter->frontEndPath, strerror(errno));
                        return;
                    }
                    /* Fire frontend active event */
                    EventsFireEventListeners(feActiveEvent, adapter);     
                    ev_io_set(&adapter->frontendWatcher, adapter->frontEndFd, EV_READ);
                    ev_io_start(loop, &adapter->frontendWatcher);
                }
                else
                {
                    ev_io_stop(loop, &adapter->frontendWatcher);
                     /* Stop all filters */
                    DVBDemuxStopAllFilters(adapter);
                    /* Close frontend */
                    close(adapter->frontEndFd);
                    adapter->frontEndFd = -1;
                    /* Fire frontend idle event */
                    EventsFireEventListeners(feIdleEvent, adapter);
                }
                break;
        }

        if (retune)
        {
            adapter->frontEndParams.frequency = adapter->frontEndRequestedFreq;
            LogModule(LOG_DEBUG, DVBADAPTER, "Tuning to %d", adapter->frontEndParams.frequency);
            if (adapter->info.type == FE_QPSK)
            {
                DVBFrontEndSatelliteSetup(adapter, &adapter->frontEndParams, &adapter->diseqcSettings);
            }

            if (ioctl(adapter->frontEndFd, FE_SET_FRONTEND, &adapter->frontEndParams) < 0)
            {
                LogModule(LOG_ERROR, DVBADAPTER, "setfront front: %s\n", strerror(errno));
            }
        }
    }
}

static void DVBFrontendCallback(struct ev_loop *loop, ev_io *w, int revents)
{
    DVBAdapter_t *adapter = w->data;
    struct dvb_frontend_event event;
    bool feLocked = adapter->frontEndLocked;
    ev_io_start(loop, w);
    if (ioctl(adapter->frontEndFd, FE_GET_EVENT, &event) == 0)
    {
        if (event.status & FE_HAS_LOCK)
        {
            feLocked = TRUE;
        }
        else
        {
            feLocked = FALSE;
        }


        if (feLocked != adapter->frontEndLocked)
        {
            adapter->frontEndLocked = feLocked;
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
        }

        if (event.parameters.frequency <= 0)
        {
            EventsFireEventListeners(tuningFailedEvent, adapter);
        }
    }
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

