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

fileadapter.c

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
#include "main.h"


/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/
#define MONITOR_CMD_EXIT              0
#define MONITOR_CMD_RETUNING          1
#define MONITOR_CMD_FE_ACTIVATE       2
#define MONITOR_CMD_FE_DEACTIVATE     3

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static int DVBOpenAdapterFile(DVBAdapter_t *adapter);
static int DVBOpenStreamFile(int adapter, uint32_t freq, int *fd, unsigned long *rate);
static void DVBFrontEndMonitorSend(DVBAdapter_t *adapter, char cmd);
static void *DVBFrontEndMonitor(void *arg);

static char *DVBEventToString(Event_t event, void *payload);

static int DVBPropertyNameGet(void *userArg, PropertyValue_t *value);
static int DVBPropertyActiveGet(void *userArg, PropertyValue_t *value);
static int DVBPropertyActiveSet(void *userArg, PropertyValue_t *value);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static const char FILEADAPTER[] = "FileAdapter";
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

static int sendFd;

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
DVBAdapter_t *DVBInit(int adapter, bool hwRestricted)
{
    DVBAdapter_t *result = NULL;
    int monitorFds[2];
    int sendRecvFds[2];

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

        strcpy(result->info.name, "File Adapter");
        if (DVBOpenAdapterFile(result) == -1)
        {
            LogModule(LOG_ERROR, FILEADAPTER, "Failed to processs adapter file!\n");
            DVBDispose(result);
            return NULL;
        }
        
        if (pipe(sendRecvFds) == -1)
        {
            LogModule(LOG_ERROR, FILEADAPTER, "Failed to create pipe : %s\n", strerror(errno));
            DVBDispose(result);
            return NULL;
        }

        if (fcntl(sendRecvFds[0], F_SETFL, O_NONBLOCK))
        {
            LogModule(LOG_INFO, FILEADAPTER, "Failed to set O_NONBLOCK on receiver (%s)\n",strerror(errno));
        }

        if (fcntl(sendRecvFds[1], F_SETFL, O_NONBLOCK))
        {
            LogModule(LOG_INFO, FILEADAPTER, "Failed to set O_NONBLOCK on sender (%s)\n",strerror(errno));
        }
        
        result->dvrFd = sendRecvFds[0];
        sendFd = sendRecvFds[1];


        if (pipe(monitorFds) == -1)
        {
            LogModule(LOG_ERROR, FILEADAPTER, "Failed to create pipe : %s\n", strerror(errno));
            DVBDispose(result);
            return NULL;
        }

        result->monitorRecvFd = monitorFds[0];
        result->monitorSendFd = monitorFds[1];

        result->hardwareRestricted = hwRestricted;
        /* Stream the entire TS to the DVR device */
        if (hwRestricted)
        {
            LogModule(LOG_INFO, FILEADAPTER, "Running in hardware restricted mode!\n");
        }
        else
        {
            DVBDemuxAllocateFilter(result, 8192, TRUE);
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
        LogModule(LOG_DEBUGV, FILEADAPTER, "Closing DVR file descriptor\n");
        close(adapter->dvrFd);
    }

    LogModule(LOG_DEBUGV, FILEADAPTER, "Closing Demux file descriptors\n");
    DVBDemuxReleaseAllFilters(adapter, FALSE);
    DVBDemuxReleaseAllFilters(adapter, TRUE);
    adapter->monitorExit = TRUE;

    if (adapter->frontEndFd > -1)
    {
        LogModule(LOG_DEBUGV, FILEADAPTER, "Closing Frontend file descriptor\n");
        close(adapter->frontEndFd);
        LogModule(LOG_DEBUGV, FILEADAPTER, "Closed Frontend file descriptor\n");
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

    DVBFrontEndMonitorSend(adapter, MONITOR_CMD_RETUNING);
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


int DVBFrontEndStatus(DVBAdapter_t *adapter, fe_status_t *status,
                            unsigned int *ber, unsigned int *strength,
                            unsigned int *snr, unsigned int *ucblock)
{
    if (status)
    {
        if (adapter->frontEndLocked)
        {
            *status = FE_HAS_LOCK | FE_HAS_CARRIER | FE_HAS_SIGNAL | FE_HAS_VITERBI;
        }
        else
        {
            *status = 0;
        }
    }

    if (ber)
    {
        if(adapter->frontEndLocked)
        {
            *ber = 0;
        }
        else
        {
            *ber = 0xffffffff;
        }
    }
    if (strength)
    {
        if(adapter->frontEndLocked)
        {
            *strength = 0xffff;
        }
        else
        {
            *strength = 0;
        }
    }

    if (snr)
    {
        if(adapter->frontEndLocked)
        {
            *snr = 0xffff;
        }
        else
        {
            *snr = 0;
        }
    }
    if (ucblock)
    {
        *ucblock = 0;
    }
    return 0;
}

int DVBFrontEndSetActive(DVBAdapter_t *adapter, bool active)
{
    if (active && (adapter->frontEndFd == -1))
    {
        /* Signal monitor thread */
        DVBFrontEndMonitorSend(adapter, MONITOR_CMD_FE_ACTIVATE);
        /* Fire frontend active event */
        EventsFireEventListeners(feActiveEvent, adapter);
        return 0;
    }

    if (!active && (adapter->frontEndFd != -1))
    {
        /* Signal monitor thread */
        DVBFrontEndMonitorSend(adapter, MONITOR_CMD_FE_DEACTIVATE);
        /* Fire frontend idle event */
        EventsFireEventListeners(feIdleEvent, adapter);
    }
    return 0;
}

int DVBDemuxSetBufferSize(DVBAdapter_t *adapter, unsigned long size)
{
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
            LogModule(LOG_DEBUG, FILEADAPTER, "Allocation filter for pid 0x%x type %s\n", pid, system ? "System":"Service");
            adapter->filters[idxToUse].demuxFd = 1;
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
                LogModule(LOG_DEBUG, FILEADAPTER, "Releasing filter for pid 0x%x type %s\n",
                    pid, adapter->filters[i].system ? "System":"Service");
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
    LogModule(LOG_DEBUG, FILEADAPTER, "Releasing all filters for type %s\n",
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
        LogModule(LOG_ERROR, FILEADAPTER, "Failed to write to monitor pipe!");
    }
}

static void *DVBFrontEndMonitor(void *arg)
{
    DVBAdapter_t *adapter = arg;
    unsigned long rate;
    #define MAX_FDS 1
    struct pollfd pfd[MAX_FDS];
    char cmd;
    unsigned long bitsSent = 0;
    char buffer[188 * 10];
    int pollDelay = -1;
    pfd[0].fd = adapter->monitorRecvFd;
    pfd[0].events = POLLIN;

    while (!adapter->monitorExit)
    {
        int n = poll(pfd, 1, pollDelay);
        if (n > 0)
        {
            if ((pfd[0].revents & POLLIN) == POLLIN)
            {

                if (read(adapter->monitorRecvFd, &cmd, 1) == 1)
                {
                    switch (cmd)
                    {
                        case MONITOR_CMD_EXIT: /* Exit */
                            break;
                        case MONITOR_CMD_RETUNING:
                            adapter->frontEndLocked = FALSE;
                            EventsFireEventListeners(unlockedEvent, adapter);

                        case MONITOR_CMD_FE_ACTIVATE:
                            /* Open description file for freq */
                            if (DVBOpenStreamFile(adapter->adapter, adapter->frontEndRequestedFreq, &adapter->frontEndFd, &rate) == 0)
                            {
                                adapter->frontEndLocked = TRUE;
                                EventsFireEventListeners(lockedEvent, adapter);
                                pollDelay = 10;
                            }
                            else
                            {
                                pollDelay = -1;
                            }
                            break;
                        case MONITOR_CMD_FE_DEACTIVATE:
                            close(adapter->frontEndFd);
                            adapter->frontEndFd = -1;
                            pollDelay = -1;
                            break;
                    }
                }
            }
        }
        else if (n == -1)
        {
            break;
        }
        if (adapter->frontEndFd != -1)
        {
            int r = read(adapter->frontEndFd, buffer, sizeof(buffer));
            if (r <= 0)
            {
                lseek(adapter->frontEndFd, 0, SEEK_SET);
            }
            else
            {
                bitsSent += r * 8;
                if (write(sendFd, buffer, r) == -1)
                {
                    /* do nothing */
                }
            }
            usleep(100);
        }
    }
    close(sendFd);
    LogModule(LOG_DEBUG, FILEADAPTER, "Monitoring thread exited.\n");
    return NULL;
}

static int DVBOpenAdapterFile(DVBAdapter_t *adapter)
{
    int result = -1;
    char *nl;
    FILE *fp;
    char path[PATH_MAX];
    char type[10];
    sprintf(path, "%s/file%d/info", DataDirectory, adapter->adapter);
    fp = fopen(path, "r");
    if (fp)
    {
        if (fgets(type, sizeof(type) -1, fp) != NULL)
        {

            nl = strchr(type, '\n');
            if (nl)
            {
                *nl = 0;
            }
            nl = strchr(type, '\r');
            if (nl)
            {
                *nl = 0;
            }
            if (strcasecmp(type, "DVB-T") == 0)
            {
                adapter->info.type = FE_OFDM;
                result = 0;
            }
            if (strcasecmp(type, "DVB-S") == 0)
            {
                adapter->info.type = FE_QPSK;
                result = 0;
            }
            if (strcasecmp(type, "DVB-C") == 0)
            {
                adapter->info.type = FE_QAM;
                result = 0;
            }
            if (strcasecmp(type, "ATSC") == 0)
            {
                adapter->info.type = FE_ATSC;
                result = 0;
            }
        }
    }
    return result;
}

static int DVBOpenStreamFile(int adapter, uint32_t freq, int *fd, unsigned long *rate)
{
    int result = -1;
    FILE *fp;
    char *nl;
    char path[PATH_MAX];
    unsigned long temp_rate = 0;

    sprintf(path, "%s/file%d/%u", DataDirectory, adapter, freq);
    fp = fopen(path, "r");
    if (fp)
    {
        if (fgets(path, sizeof(path) - 1, fp) != NULL)
        {
            nl = strchr(path, '\n');
            if (nl)
            {
                *nl = 0;
            }
            nl = strchr(path, '\r');
            if (nl)
            {
                *nl = 0;
            }
            LogModule(LOG_DEBUG, FILEADAPTER, "Opening stream file %s", path);
            if (fscanf(fp, "%lu", &temp_rate) == 1)
            {
                LogModule(LOG_DEBUG, FILEADAPTER, "Stream rate : %lu bps (UNUSED)", temp_rate);
                if (temp_rate > 0)
                {
                    *fd = open(path, O_RDONLY);
                    if (*fd != -1)
                    {
                        *rate = temp_rate;
                        result = 0;
                    }
                }
            }
        }
    }
    return result;
}

static char *DVBEventToString(Event_t event, void *payload)
{
    char *result = NULL;
    DVBAdapter_t *adapter = payload;
    if (asprintf(&result, "%d", adapter->adapter) == -1)
    {
        LogModule(LOG_ERROR, FILEADAPTER, "Failed to allocate memory for event description when converting event to string\n");
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

