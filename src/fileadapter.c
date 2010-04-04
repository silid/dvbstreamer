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
#include <yaml.h>

#include "types.h"
#include "dvbadapter.h"
#include "logging.h"
#include "objects.h"
#include "events.h"
#include "properties.h"
#include "main.h"
#include "dispatchers.h"
#include "yamlutils.h"

/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/
#define MONITOR_CMD_EXIT              0
#define MONITOR_CMD_RETUNING          1
#define MONITOR_CMD_FE_ACTIVATE       2
#define MONITOR_CMD_FE_DEACTIVATE     3
/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/
struct DVBAdapter_s
{
    int adapter;                      /**< The adapter number ie /dev/dvb/adapter<#adapter> */

    DVBSupportedDeliverySys_t *supportedDelSystems;

    int frontEndFd;                   /**< File descriptor for the frontend device */
    bool frontEndLocked;              /**< Whether the frontend is currently locked onto a signal. */

    DVBDeliverySystem_e currentDeliverySystem;
    char *frontEndParams;
    __u32 frontEndRequestedFreq;      /**< The frequency that the application requested, may be different from one used (ie DVB-S intermediate frequency) */

    LNBInfo_t lnbInfo;                /**< LNB Information for DVB-S/S2 receivers */

    bool hardwareRestricted;          /**< Whether the adapter can only stream a
                                           portion of the transport stream */
    int maxFilters;                   /**< Maximum number of available filters. */
    DVBAdapterPIDFilter_t filters[DVB_MAX_PID_FILTERS];/**< File descriptor for the demux device.*/

    int dvrFd;                        /**< File descriptor for the dvr device */

    int cmdRecvFd;                    /**< File descriptor for monitor task to recieve commands */
    int cmdSendFd;                    /**< File descriptor to send commands to monitor task. */
    ev_io commandWatcher;
    int sendFd;
    ev_timer sendTimer; 
} ;

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static int DVBOpenAdapterFile(DVBAdapter_t *adapter);
static int DVBOpenStreamFile(int adapter, uint32_t freq, int *fd, unsigned long *rate);
static void DVBFrontEndMonitorSend(DVBAdapter_t *adapter, char cmd);
static void DVBCommandCallback(struct ev_loop *loop, ev_io *w, int revents);
static void DVBFilterPackets(struct ev_loop *loop, ev_timer *w, int revents);
static int DVBEventToString(yaml_document_t *document, Event_t event, void *payload);

static int DVBPropertyActiveGet(void *userArg, PropertyValue_t *value);
static int DVBPropertyActiveSet(void *userArg, PropertyValue_t *value);
static int DVBPropertyDeliverySystemsGet(void *userArg, PropertyValue_t *value);
static uint32_t ConvertStringToUInt32(const char *str, uint32_t defaultValue);
static uint32_t ConvertYamlNode(yaml_document_t * document, const char *key, 
                        uint32_t (*convert)(const char *, uint32_t), uint32_t defaultValue);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static const char FILEADAPTER[] = "FileAdapter";
static const char propertyParent[] = "adapter";
static const char adapterName[] = "File Adapter";
static EventSource_t dvbSource = NULL;
static Event_t lockedEvent;
static Event_t unlockedEvent;
static Event_t tuningFailedEvent;
static Event_t feActiveEvent;
static Event_t feIdleEvent;

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
DVBAdapter_t *DVBInit(int adapter, bool hwRestricted)
{
    DVBAdapter_t *result = NULL;
    int monitorFds[2];
    int sendRecvFds[2];
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
    
    ObjectRegisterType(DVBAdapter_t);
    ObjectRegisterCollection(TOSTRING(DVBSupportedDeliverySys_t), sizeof(DVBDeliverySystem_e), NULL);
    result = (DVBAdapter_t*)ObjectCreateType(DVBAdapter_t);
    
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
        result->sendFd = sendRecvFds[1];

        if (pipe(monitorFds) == -1)
        {
            LogModule(LOG_ERROR, FILEADAPTER, "Failed to create pipe : %s\n", strerror(errno));
            DVBDispose(result);
            return NULL;
        }

        result->cmdRecvFd= monitorFds[0];
        result->cmdSendFd = monitorFds[1];

        result->hardwareRestricted = hwRestricted;
        if (hwRestricted)
        {
            result->maxFilters = 16;
        }
        else
        {
            result->maxFilters = 256;
        }
        
        inputLoop = DispatchersGetInput();
        ev_io_init(&result->commandWatcher, DVBCommandCallback, result->cmdRecvFd, EV_READ);
        ev_timer_init(&result->sendTimer, DVBFilterPackets, 0.1, 0.1);
        result->sendTimer.data = result;
        result->commandWatcher.data = result;
        ev_timer_start(inputLoop, &result->sendTimer);
        ev_io_start(inputLoop, &result->commandWatcher);   

        /* Add properties */
        PropertiesAddSimpleProperty(propertyParent, "number", "The number of the adapter being used",
            PropertyType_Int, &result->adapter, SIMPLEPROPERTY_R);
        PropertiesAddSimpleProperty(propertyParent, "name", "Hardware driver name",
            PropertyType_String, (void*)adapterName, SIMPLEPROPERTY_R);
        PropertiesAddSimpleProperty(propertyParent, "hwrestricted", "Whether the hardware is not capable of supplying the entire TS.",
            PropertyType_Boolean, &result->hardwareRestricted, SIMPLEPROPERTY_R);
        PropertiesAddProperty(propertyParent, "systems", "The broadcast systems the frontend is capable of receiving",
            PropertyType_String, result, DVBPropertyDeliverySystemsGet, NULL);
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
        LogModule(LOG_DEBUGV, FILEADAPTER, "Closing DVR file descriptor\n");
        close(adapter->dvrFd);
        close(adapter->sendFd);
    }

    LogModule(LOG_DEBUGV, FILEADAPTER, "Closing Demux file descriptors\n");
    DVBDemuxReleaseAllFilters(adapter);

    if (adapter->frontEndFd > -1)
    {
        LogModule(LOG_DEBUGV, FILEADAPTER, "Closing Frontend file descriptor\n");
        close(adapter->frontEndFd);
        LogModule(LOG_DEBUGV, FILEADAPTER, "Closed Frontend file descriptor\n");
        adapter->frontEndFd = -1;
    }

    ev_io_stop(inputLoop, &adapter->commandWatcher);
    ev_timer_stop(inputLoop, &adapter->sendTimer);
    
    close(adapter->cmdRecvFd);
    close(adapter->cmdSendFd);

    PropertiesRemoveAllProperties(propertyParent);
    ObjectRefDec(adapter->supportedDelSystems);
    ObjectRefDec(adapter);
}

DVBSupportedDeliverySys_t *DVBFrontEndGetDeliverySystems(DVBAdapter_t *adapter)
{
    return adapter->supportedDelSystems;
}

bool DVBFrontEndDeliverySystemSupported(DVBAdapter_t * adapter,DVBDeliverySystem_e system)
{
    int i;
    for (i = 0; i < adapter->supportedDelSystems->nrofSystems; i ++)
    {
        if (adapter->supportedDelSystems->systems[i] == system)
        {
            return TRUE;
        }
    }
    return FALSE;
}


int DVBFrontEndTune(DVBAdapter_t *adapter, DVBDeliverySystem_e system, char *params)
{
    yaml_document_t document;
    memset(&document, 0, sizeof(document));
    if (YamlUtils_Parse(params, &document))
    {
        adapter->currentDeliverySystem = system;
        adapter->frontEndParams = strdup(params);
        adapter->frontEndRequestedFreq = ConvertYamlNode(&document, "Frequency",  ConvertStringToUInt32, 0);
        yaml_document_delete(&document);
        DVBFrontEndMonitorSend(adapter, MONITOR_CMD_RETUNING);
    }

    return 0;
}

char* DVBFrontEndParametersGet(DVBAdapter_t *adapter, DVBDeliverySystem_e *system)
{
    *system = adapter->currentDeliverySystem;
    return strdup(adapter->frontEndParams);
}

bool DVBFrontEndParameterSupported(DVBAdapter_t *adapter,DVBDeliverySystem_e system, char *param, char *value)
{
    return TRUE;
}


void DVBFrontEndLNBInfoSet(DVBAdapter_t *adapter, LNBInfo_t *info)
{
    adapter->lnbInfo = *info;
}

bool DVBFrontEndIsLocked(DVBAdapter_t *adapter)
{
    return adapter->frontEndLocked;
}

int DVBFrontEndStatus(DVBAdapter_t *adapter, DVBFrontEndStatus_e *status,
                            unsigned int *ber, unsigned int *strength,
                            unsigned int *snr, unsigned int *ucblock)
{
    if (status)
    {
        if (adapter->frontEndLocked)
        {
            *status = FESTATUS_HAS_LOCK | FESTATUS_HAS_CARRIER | FESTATUS_HAS_SIGNAL | FESTATUS_HAS_VITERBI;
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

int DVBDemuxGetMaxFilters(DVBAdapter_t *adapter)
{
    return adapter->maxFilters;
}

int DVBDemuxSetBufferSize(DVBAdapter_t *adapter, unsigned long size)
{
    return 0;
}

bool DVBDemuxIsHardwareRestricted(DVBAdapter_t *adapter)
{
    return adapter->hardwareRestricted;
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
        LogModule(LOG_DEBUG, FILEADAPTER, "Allocated filter for pid 0x%x\n", pid);
        adapter->filters[idxToUse].demuxFd = 1;
        adapter->filters[idxToUse].pid = pid;
        result = 0;
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
                LogModule(LOG_DEBUG, FILEADAPTER, "Releasing filter for pid 0x%x\n", pid);
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
    LogModule(LOG_DEBUG, FILEADAPTER, "Releasing all filters\n");
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

int DVBDVRGetFD(DVBAdapter_t *adapter)
{
    return adapter->dvrFd;
}



static void DVBFrontEndMonitorSend(DVBAdapter_t *adapter, char cmd)
{
    if (write(adapter->cmdSendFd, &cmd, 1) != 1)
    {
        LogModule(LOG_ERROR, FILEADAPTER, "Failed to write to monitor pipe!");
    }
}

static void DVBCommandCallback(struct ev_loop *loop, ev_io *w, int revents)
{
    DVBAdapter_t *adapter = w->data;
    unsigned long rate;
    char cmd;
    ev_io_start(loop, w);
    
    if (read(adapter->cmdRecvFd, &cmd, 1) == 1)
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
                    ev_timer_set(&adapter->sendTimer, 0.1,0.1);
                    ev_timer_start(loop, &adapter->sendTimer);
                }
                else
                {
                    ev_timer_stop(loop, &adapter->sendTimer);
                }
                break;
            case MONITOR_CMD_FE_DEACTIVATE:
                close(adapter->frontEndFd);
                adapter->frontEndFd = -1;
                ev_timer_stop(loop, &adapter->sendTimer);
                break;
        }
    }
}

static void DVBFilterPackets(struct ev_loop *loop, ev_timer *w, int revents)
{
    DVBAdapter_t *adapter = w->data;
    char buffer[188 * 10];
    if (adapter->frontEndFd != -1)
    {
        int r = read(adapter->frontEndFd, buffer, sizeof(buffer));
        if (r <= 0)
        {
            lseek(adapter->frontEndFd, 0, SEEK_SET);
        }
        else
        {
            int i, p;

            for (i = 0; i < r/188; i ++)
            {
                TSPacket_t *packet = (TSPacket_t*)&buffer[i * 188];
                uint16_t pid = TSPACKET_GETPID(*packet);
                for (p = 0; p < adapter->maxFilters; p ++)
                {
                    if (((adapter->filters[p].pid == 8192) || (pid == adapter->filters[p].pid)) && 
                         (adapter->filters[p].demuxFd != -1))
                    {
                        if (write(adapter->sendFd, packet, 188) == -1)
                        {
                            /* do nothing */
                        }
                        break;
                    }
                }
            }
        }
    }
}


static int DVBOpenAdapterFile(DVBAdapter_t *adapter)
{
    int result = -1;
    char *nl;
    FILE *fp;
    char path[PATH_MAX];
    char type[10];
   
    adapter->supportedDelSystems = (DVBSupportedDeliverySys_t*)ObjectCollectionCreate(TOSTRING(DVBSupportedDeliverySys_t),1);

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
                adapter->supportedDelSystems->systems[0] = DELSYS_DVBT;
                result = 0;
            }
            if (strcasecmp(type, "DVB-S") == 0)
            {
                adapter->supportedDelSystems->systems[0] = DELSYS_DVBS;
                result = 0;
            }
            if (strcasecmp(type, "DVB-C") == 0)
            {
                adapter->supportedDelSystems->systems[0] = DELSYS_DVBC;
                result = 0;
            }
            if (strcasecmp(type, "ATSC") == 0)
            {
                adapter->supportedDelSystems->systems[0] = DELSYS_ATSC;
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

static int DVBEventToString(yaml_document_t *document, Event_t event, void *payload)
{
    DVBAdapter_t *adapter = payload;
    char adapterStr[4];
    int mappingId = yaml_document_add_mapping(document, (yaml_char_t*)YAML_MAP_TAG, YAML_ANY_MAPPING_STYLE);
    sprintf(adapterStr, "%d", adapter->adapter);
    YamlUtils_MappingAdd(document, mappingId, "Adapter", adapterStr);
    return mappingId;
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

static int DVBPropertyDeliverySystemsGet(void *userArg, PropertyValue_t *value)
{
    DVBAdapter_t *adapter = userArg;
    int i;
    int size = 0;

    for (i = 0; i < adapter->supportedDelSystems->nrofSystems; i ++)
    {
        size +=  2 + strlen(DVBDeliverySystemStr[adapter->supportedDelSystems->systems[i]]) + 1;
    }

    value->u.string = malloc(size + 1);
    value->u.string[0] = 0;
    for (i = 0; i < adapter->supportedDelSystems->nrofSystems; i ++)
    {
        sprintf(value->u.string + strlen(value->u.string), 
                "- %s\n", DVBDeliverySystemStr[adapter->supportedDelSystems->systems[i]]);
    }
    return 0;
}

static uint32_t ConvertStringToUInt32(const char *str, uint32_t defaultValue)
{
    char *suffix;
    uint32_t result = strtoul(str, &suffix, 10);
    if (suffix == str)
    {
        result = defaultValue;
    }
    return result;
}

static uint32_t ConvertYamlNode(yaml_document_t * document, const char *key, 
                        uint32_t (*convert)(const char *, uint32_t), uint32_t defaultValue)
{
    yaml_node_t *node = YamlUtils_RootMappingFind(document, key);
    if (node && (node->type == YAML_SCALAR_NODE))
    {
        return convert((const char*)node->data.scalar.value, defaultValue);
    }
    return defaultValue;

}

