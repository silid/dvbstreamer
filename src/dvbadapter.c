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
#include <yaml.h>

#include "types.h"
#include "dvbadapter.h"
#include "logging.h"
#include "objects.h"
#include "events.h"
#include "properties.h"
#include "dispatchers.h"
#include "yamlutils.h"

/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/
#define DVB_CMD_TUNE              0
#define DVB_CMD_FE_ACTIVE_TOGGLE  1

/* Use LinuxDVB Version 3 API */
#define USE_V3 1

/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/

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
typedef struct DVBSatelliteSettings_s
{
    enum Polarisation_e polarisation;/**< Polarisation of the signal */
    unsigned int satellite_number;  /**< Satellite number for the switch */
}DVBSatelliteSettings_t;


struct DVBAdapter_s
{
    int adapter;                      /**< The adapter number ie /dev/dvb/adapter<#adapter> */

    struct dvb_frontend_info info;    /**< Information about the front end */
    DVBSupportedDeliverySys_t *supportedDelSystems;

    char frontEndPath[30];            /**< Path to the frontend device */
    int frontEndFd;                   /**< File descriptor for the frontend device */
    bool frontEndLocked;              /**< Whether the frontend is currently locked onto a signal. */

    DVBDeliverySystem_e currentDeliverySystem;
    __u32 frontEndRequestedFreq;      /**< The frequency that the application requested, may be different from one used (ie DVB-S intermediate frequency) */

#if (DVB_API_VERSION < 5) || defined(USE_V3)
    struct dvb_frontend_parameters frontEndParams; /**< The current frontend configuration parameters. These may be updated when the frontend locks. */
    DVBSatelliteSettings_t satelliteSettings; /**< Current DiSEqC settings for DVB-S */
#else

#endif
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
} ;

typedef struct StringToParamMapping_s
{
    char *str;
    uint32_t param;
}StringToParamMapping_t;

#define STRINGTOPARAMMAPPING_SENTINEL {NULL, 0 }

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static int DVBFrontEndSatelliteSetup(DVBAdapter_t *adapter, struct dvb_frontend_parameters *frontend, DVBSatelliteSettings_t *diseqc);
static int DVBFrontEndDiSEqCSet(DVBAdapter_t *adapter, DVBSatelliteSettings_t *diseqc, bool tone);
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
static int DVBPropertyDeliverySystemsGet(void *userArg, PropertyValue_t *value);

static char * MapValueToString(StringToParamMapping_t *mapping, uint32_t value, char *defaultValue);
static uint32_t MapStringToValue(StringToParamMapping_t *mapping, const char *str, uint32_t defaultValue);
static uint32_t MapYamlNode(yaml_document_t * document, const char *key, StringToParamMapping_t *mapping, uint32_t defaultValue);
static uint32_t ConvertStringToUInt32(const char *str, uint32_t defaultValue);
static uint32_t ConvertYamlNode(yaml_document_t * document, const char *key, 
                        uint32_t (*convert)(const char *, uint32_t), uint32_t defaultValue);

#if (DVB_API_VERSION < 5) || defined(USE_V3)
static void ConvertYamlToFEParams(DVBDeliverySystem_e delSys, yaml_document_t *doc, struct dvb_frontend_parameters *feparams, DVBSatelliteSettings_t *satSettings);
static void ConvertFEParamsToYaml(DVBDeliverySystem_e delSys, struct dvb_frontend_parameters *feparams, DVBSatelliteSettings_t *satSettings, yaml_document_t *doc);
#else 
static uint32_t ConvertStringToBandwith(const char *str, uint32_t defaultValue);
static int ConvertYamlToDTVProperties(DVBDeliverySystem_e delSys, yaml_document_t *doc, struct dtv_properties *feparams);
static void ConvertDTVPropertiesToYaml(DVBDeliverySystem_e delSys, struct dtv_properties *feparams, yaml_document_t *doc);
#endif


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

static const char TAG_FREQUENCY[]        = "Frequency";
static const char TAG_INVERSION[]        = "Inversion";
static const char TAG_FEC[]              = "FEC";
static const char TAG_SYMBOL_RATE[]      = "Symbol Rate";
static const char TAG_MODULATION[]       = "Modulation";
static const char TAG_BANDWIDTH[]        = "Bandwidth";
static const char TAG_FEC_HP[]           = "FEC HP";
static const char TAG_FEC_LP[]           = "FEC LP";
static const char TAG_CONSTELLATION[]    = "Constellation";
static const char TAG_GUARD_INTERVAL[]   = "Guard Interval";
static const char TAG_TRANSMISSION_MODE[]= "Transmission Mode";
static const char TAG_HIERARCHY[]        = "Hierarchy";
static const char TAG_POLARISATION[]     = "Polarisation";
static const char TAG_SATELLITE_NUMBER[] = "Satellite Number";

static StringToParamMapping_t modulationMapping[] = {
    {"QPSK", QPSK},
    {"QAM16", QAM_16}, {"16QAM", QAM_16},        
    {"QAM32", QAM_32}, {"32QAM", QAM_32},
    {"QAM64", QAM_64}, {"64QAM", QAM_64},
    {"QAM128", QAM_128}, {"128QAM", QAM_128},
    {"QAM256", QAM_256}, {"256QAM", QAM_256},
    {"AUTO", QAM_AUTO},
    {"8VSB", VSB_8},
    {"16VSB", VSB_16},
#if  DVB_API_VERSION >= 5   
    {"8PSK", PSK_8},    {"PSK8", PSK_8},
    {"16APSK", APSK_16},{"APSK16", APSK_16},
    {"32APSK", APSK_32},{"APSK32", APSK_32},        
    {"DQPSK", DQPSK}
#endif
    STRINGTOPARAMMAPPING_SENTINEL
};

static StringToParamMapping_t inversionMapping[] = {
    {"OFF", INVERSION_OFF},
    {"ON", INVERSION_ON},
    {"AUTO", INVERSION_AUTO},
    STRINGTOPARAMMAPPING_SENTINEL
};

static StringToParamMapping_t fecMapping[] = {
    {"NONE", FEC_NONE},
    {"1/2", FEC_1_2},
    {"2/3", FEC_2_3},
    {"3/4", FEC_3_4},
    {"4/5", FEC_4_5},
    {"5/6", FEC_5_6},        
    {"6/7", FEC_6_7},
    {"7/8", FEC_7_8},
    {"8/9", FEC_8_9},
    {"AUTO", FEC_AUTO},
#if  DVB_API_VERSION >= 5   
    {"3/5", FEC_3_5},
    {"9/10", FEC_9_10},
#endif
    STRINGTOPARAMMAPPING_SENTINEL
};

#if (DVB_API_VERSION < 5) || defined(USE_V3)
static StringToParamMapping_t transmissonModeMapping[] = {
    {"2000", TRANSMISSION_MODE_2K}, {"2k", TRANSMISSION_MODE_2K}, 
    {"8000", TRANSMISSION_MODE_2K}, {"8k", TRANSMISSION_MODE_8K}, 
    {"AUTO", TRANSMISSION_MODE_AUTO},
    STRINGTOPARAMMAPPING_SENTINEL
};

static StringToParamMapping_t bandwidthMapping[] = {
    {"8000000", BANDWIDTH_8_MHZ}, {"8Mhz", BANDWIDTH_8_MHZ},
    {"7000000", BANDWIDTH_7_MHZ}, {"7Mhz", BANDWIDTH_7_MHZ},
    {"6000000", BANDWIDTH_6_MHZ}, {"6Mhz", BANDWIDTH_6_MHZ},
    {"AUTO", BANDWIDTH_AUTO},
    STRINGTOPARAMMAPPING_SENTINEL
};
#endif

static StringToParamMapping_t guardIntervalMapping[] = {
    {"1/32", GUARD_INTERVAL_1_32},
    {"1/16", GUARD_INTERVAL_1_16},
    {"1/8", GUARD_INTERVAL_1_8},
    {"1/4", GUARD_INTERVAL_1_4},
    {"AUTO", GUARD_INTERVAL_AUTO},
    STRINGTOPARAMMAPPING_SENTINEL
};

static StringToParamMapping_t hierarchyMapping[] = {
    {"NONE", HIERARCHY_NONE},
    {"1", HIERARCHY_1},
    {"2", HIERARCHY_2},
    {"4", HIERARCHY_4},
    {"AUTO", HIERARCHY_AUTO},
    STRINGTOPARAMMAPPING_SENTINEL
};

static StringToParamMapping_t polarisationMapping[] = {
    {"Horizontal", POL_HORIZONTAL},
    {"Vertical", POL_VERTICAL},
    {"Left", POL_HORIZONTAL},
    {"Right", POL_VERTICAL},
    STRINGTOPARAMMAPPING_SENTINEL
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
    ObjectRegisterType(DVBAdapter_t);
    ObjectRegisterCollection(TOSTRING(DVBSupportedDeliverySys_t), sizeof(DVBDeliverySystem_e), NULL);
    result = (DVBAdapter_t*)ObjectCreateType(DVBAdapter_t);
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
        result->currentDeliverySystem = DELSYS_MAX_SUPPORTED;
        switch (result->info.type)
        {
            case FE_QPSK:
#if HAVE_FE_CAN_2G_MODULATION
                if (result->info.caps & FE_CAN_2G_MODULATION)
                {
                    result->supportedDelSystems = (DVBSupportedDeliverySys_t*)ObjectCollectionCreate(TOSTRING(DVBSupportedDeliverySys_t),2);
                    result->supportedDelSystems->systems[0] = DELSYS_DVBS;
                    result->supportedDelSystems->systems[1] = DELSYS_DVBS2;                    
                }
                else
#endif
                {
                    result->supportedDelSystems = (DVBSupportedDeliverySys_t*)ObjectCollectionCreate(TOSTRING(DVBSupportedDeliverySys_t),1);
                    result->supportedDelSystems->systems[0] = DELSYS_DVBS;
                }
                break;
            case FE_QAM:
                result->supportedDelSystems = (DVBSupportedDeliverySys_t*)ObjectCollectionCreate(TOSTRING(DVBSupportedDeliverySys_t),1);
                result->supportedDelSystems->systems[0] = DELSYS_DVBC;
                break;
            case FE_OFDM:
#if HAVE_FE_CAN_2G_MODULATION
                if (result->info.caps & FE_CAN_2G_MODULATION)
                {
                    result->supportedDelSystems = (DVBSupportedDeliverySys_t*)ObjectCollectionCreate(TOSTRING(DVBSupportedDeliverySys_t),2);
                    result->supportedDelSystems->systems[0] = DELSYS_DVBT;
                    result->supportedDelSystems->systems[1] = DELSYS_DVBT2;                    
                }
                else
#endif
                {
                    result->supportedDelSystems = (DVBSupportedDeliverySys_t*)ObjectCollectionCreate(TOSTRING(DVBSupportedDeliverySys_t),1);
                    result->supportedDelSystems->systems[0] = DELSYS_DVBT;
                }
                break;
            case FE_ATSC:
                result->supportedDelSystems = (DVBSupportedDeliverySys_t*)ObjectCollectionCreate(TOSTRING(DVBSupportedDeliverySys_t),1);
                result->supportedDelSystems->systems[0] = DELSYS_ATSC;
                break;
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
#if (DVB_API_VERSION < 5) || defined(USE_V3)
        adapter->currentDeliverySystem = system;
        ConvertYamlToFEParams(system, &document, &adapter->frontEndParams, &adapter->satelliteSettings);
        adapter->frontEndRequestedFreq = adapter->frontEndParams.frequency;
#else
        /* TODO Use new V5 API structures. */
#endif
        yaml_document_delete(&document);
        DVBCommandSend(adapter, DVB_CMD_TUNE);
   }
   return 0;
}

char* DVBFrontEndParametersGet(DVBAdapter_t *adapter, DVBDeliverySystem_e *system)
{
    char *output;
    yaml_document_t document;
    yaml_document_initialize(&document, NULL, NULL, NULL, 0, 0);
#if (DVB_API_VERSION < 5) || defined(USE_V3)
    {
        struct dvb_frontend_parameters feparams = adapter->frontEndParams;
        feparams.frequency = adapter->frontEndRequestedFreq;
        yaml_document_add_mapping(&document, (yaml_char_t*)YAML_MAP_TAG, YAML_ANY_MAPPING_STYLE);
        ConvertFEParamsToYaml(adapter->currentDeliverySystem, &feparams, &adapter->satelliteSettings, &document);
    }
#else
        /* TODO Use new V5 API structures. */
#endif
    *system = adapter->currentDeliverySystem;
    YamlUtils_DocumentToString(&document, TRUE, &output);
    return output;
}

bool DVBFrontEndParameterSupported(DVBAdapter_t *adapter, DVBDeliverySystem_e system, char *param, char *value)
{
    char *AUTO = "AUTO";
    if (DVBFrontEndDeliverySystemSupported(adapter, system))
    {
        if (strcmp(param, "Inversion") == 0)
        {
            if (strcasecmp(value, AUTO) == 0)
            {
                return adapter->info.caps & FE_CAN_INVERSION_AUTO;
            }
            if ((strcasecmp(value, "ON") == 0) || (strcasecmp(value, "OFF") == 0))
            {
                return TRUE;
            }
        }
        else if ((strcmp(param, "FEC") == 0) ||
            (strcmp(param, "FEC HP") == 0) ||
            (strcmp(param, "FEC LP") == 0))
        {
            if (strcasecmp(value, AUTO) == 0)
            {
                return adapter->info.caps & FE_CAN_FEC_AUTO;
            }
            if (strcmp(value, "1/2") == 0)
            {
                return adapter->info.caps & FE_CAN_FEC_1_2;
            }
            if (strcmp(value, "2/3") == 0)
            {
                return adapter->info.caps & FE_CAN_FEC_2_3;
            }
            if (strcmp(value, "3/4") == 0)
            {
                return adapter->info.caps & FE_CAN_FEC_3_4;
            }
            if (strcmp(value, "4/5") == 0)
            {
                return adapter->info.caps & FE_CAN_FEC_4_5;
            }
            if (strcmp(value, "5/6") == 0)
            {
                return adapter->info.caps & FE_CAN_FEC_5_6;
            }
            if (strcmp(value, "6/7") == 0)
            {
                return adapter->info.caps & FE_CAN_FEC_6_7;
            }
            if (strcmp(value, "7/8") == 0)
            {
                return adapter->info.caps & FE_CAN_FEC_7_8;
            }
            if (strcmp(value, "8/9") == 0)
            {
                return adapter->info.caps & FE_CAN_FEC_8_9;
            }
 
        }
        else if (strcmp(param, "Guard Interval") == 0)
        {
            if (strcmp(value, AUTO) == 0)
            {
                return adapter->info.caps & FE_CAN_GUARD_INTERVAL_AUTO;
            }
            if ((strcmp(value, "1/32") == 0) || (strcmp(value, "1/16") == 0) ||
                (strcmp(value, "1/8") == 0) || (strcmp(value, "1/4") == 0))
            {
                return TRUE;
            }
        }
        else if (strcmp(param, "Transmission Mode") == 0)
        {
            if (strcmp(value, AUTO) == 0)
            {
                return adapter->info.caps & FE_CAN_TRANSMISSION_MODE_AUTO;
            }
            if ((strcmp(value, "4000") == 0) || (strcmp(value, "2000") == 0))
            {
                return TRUE;
            }
            
        }
        else if (strcmp(param, "Bandwidth") == 0)
        {
            if (strcasecmp(value, AUTO) == 0)
            {
                return adapter->info.caps & FE_CAN_BANDWIDTH_AUTO;
            }
            return TRUE;
        }
        else if (strcmp(param, "Hierarchy") == 0)
        {
            if (strcasecmp(value, AUTO) == 0)
            {
                return adapter->info.caps & FE_CAN_HIERARCHY_AUTO;
            }
            if ((strcasecmp(value, "NONE") == 0) || (strcmp(value, "1") == 0) ||
                (strcmp(value, "2") == 0) || (strcmp(value, "4") == 0))
            {
                return TRUE;
            }
            return TRUE;
        }
        else if ((strcmp(param, "Modulation") == 0) || (strcmp(param, "Constellation") == 0))
        {
            if (strcasecmp(value, "QPSK") == 0)
            {
                return adapter->info.caps & FE_CAN_QPSK;
            }
            if ((strcasecmp(value, "16QAM") == 0) || (strcasecmp(value, "QAM16") == 0))
            {
                return adapter->info.caps & FE_CAN_QAM_16;
            }
            if ((strcasecmp(value, "32QAM") == 0) || (strcasecmp(value, "QAM32") == 0))
            {
                return adapter->info.caps & FE_CAN_QAM_32;
            }
            if ((strcasecmp(value, "64QAM") == 0) || (strcasecmp(value, "QAM64") == 0))
            {
                return adapter->info.caps & FE_CAN_QAM_64;
            }
            if ((strcasecmp(value, "128QAM") == 0) || (strcasecmp(value, "QAM128") == 0))
            {
                return adapter->info.caps & FE_CAN_QAM_128;
            }
            if ((strcasecmp(value, "256QAM") == 0) || (strcasecmp(value, "QAM256") == 0))
            {
                return adapter->info.caps & FE_CAN_QAM_256;
            }
            if (strcasecmp(value, "AUTO") == 0)
            {
                return adapter->info.caps & FE_CAN_QAM_AUTO;
            }
            if (strcasecmp(value, "8VSB") == 0)
            {
                return adapter->info.caps & FE_CAN_8VSB;
            }
            if (strcasecmp(value, "16VSB") == 0)
            {
                return adapter->info.caps & FE_CAN_16VSB;
            }
        }
    }
    return FALSE;
}


void DVBFrontEndLNBInfoSet(DVBAdapter_t *adapter, LNBInfo_t *lnbInfo)
{
    adapter->lnbInfo = *lnbInfo;
}

static int DVBFrontEndSatelliteSetup(DVBAdapter_t *adapter, struct dvb_frontend_parameters *frontend, DVBSatelliteSettings_t *diseqc)
{
    bool tone = FALSE;

    frontend->frequency = LNBTransponderToIntermediateFreq(&adapter->lnbInfo, frontend->frequency, &tone);

    return DVBFrontEndDiSEqCSet(adapter, diseqc, tone);
}

static int DVBFrontEndDiSEqCSet(DVBAdapter_t *adapter, DVBSatelliteSettings_t *diseqc, bool tone)
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

bool DVBFrontEndIsLocked(DVBAdapter_t *adapter)
{
    return adapter->frontEndLocked;
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

int DVBDemuxGetMaxFilters(DVBAdapter_t *adapter)
{
    return adapter->maxFilters;
}

int DVBDemuxGetAvailableFilters(DVBAdapter_t *adapter)
{
    int count = 0;
    int i;
    for (i = 0; i < adapter->maxFilters; i ++)
    {
        if (adapter->filters[i].demuxFd == -1)
        {
            count ++;
        }
    }
    return count;
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

int DVBDVRGetFD(DVBAdapter_t *adapter)
{
    return adapter->dvrFd;
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
                DVBFrontEndSatelliteSetup(adapter, &adapter->frontEndParams, &adapter->satelliteSettings);
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
    if (asprintf(&result, "Adapter: %d", adapter->adapter) == -1)
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

static char * MapValueToString(StringToParamMapping_t *mapping, uint32_t value, char *defaultValue)
{
    int i;
    for (i = 0; mapping[i].str; i ++)
    {
        if (mapping[i].param == value)
        {
            return mapping[i].str;
        }
    }
    return defaultValue;

}

static uint32_t MapStringToValue(StringToParamMapping_t *mapping, const char *str, uint32_t defaultValue)
{
    int i;
    for (i = 0; mapping[i].str; i ++)
    {
        if (strcasecmp(mapping[i].str, str) == 0)
        {
            return mapping[i].param;
        }
    }
    return defaultValue;
}

static uint32_t MapYamlNode(yaml_document_t * document, const char *key, StringToParamMapping_t *mapping, uint32_t defaultValue)
{
    yaml_node_t *node = YamlUtils_RootMappingFind(document, key);
    if (node && (node->type == YAML_SCALAR_NODE))
    {
        return MapStringToValue(mapping, (const char*)node->data.scalar.value, defaultValue);
    }
    return defaultValue;
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

#if (DVB_API_VERSION < 5) || defined(USE_V3)

static void ConvertYamlToFEParams(DVBDeliverySystem_e delSys, yaml_document_t *doc, struct dvb_frontend_parameters *feparams, DVBSatelliteSettings_t *satSettings)
{
    feparams->frequency = ConvertYamlNode(doc, TAG_FREQUENCY,  ConvertStringToUInt32, 0);
    feparams->inversion = MapYamlNode(doc, TAG_INVERSION, inversionMapping, INVERSION_AUTO);
    switch (delSys)
    {
        case DELSYS_DVBS:
            feparams->u.qpsk.fec_inner = MapYamlNode(doc, TAG_FEC, fecMapping, FEC_AUTO);
            feparams->u.qpsk.symbol_rate = ConvertYamlNode(doc, TAG_SYMBOL_RATE, ConvertStringToUInt32, 0);
            satSettings->polarisation = MapYamlNode(doc, TAG_POLARISATION, polarisationMapping, POL_HORIZONTAL);
            satSettings->satellite_number = ConvertYamlNode(doc, TAG_SATELLITE_NUMBER,  ConvertStringToUInt32, 0);
            break;
        case DELSYS_DVBC:
            feparams->u.qam.fec_inner = MapYamlNode(doc, TAG_FEC, fecMapping, FEC_AUTO);
            feparams->u.qam.symbol_rate = ConvertYamlNode(doc, TAG_SYMBOL_RATE, ConvertStringToUInt32, 0);
            feparams->u.qam.modulation = MapYamlNode(doc, TAG_MODULATION, modulationMapping, QAM_AUTO);
            break;
        case DELSYS_DVBT:
            feparams->u.ofdm.bandwidth = MapYamlNode(doc, TAG_BANDWIDTH, bandwidthMapping, BANDWIDTH_AUTO);
            feparams->u.ofdm.code_rate_HP = MapYamlNode(doc, TAG_FEC_HP, fecMapping, FEC_AUTO);
            feparams->u.ofdm.code_rate_LP = MapYamlNode(doc, TAG_FEC_LP, fecMapping, FEC_AUTO);
            feparams->u.ofdm.constellation = MapYamlNode(doc, TAG_CONSTELLATION, modulationMapping, QAM_AUTO);
            feparams->u.ofdm.guard_interval = MapYamlNode(doc,TAG_GUARD_INTERVAL, guardIntervalMapping, GUARD_INTERVAL_AUTO);
            feparams->u.ofdm.transmission_mode = MapYamlNode(doc, TAG_TRANSMISSION_MODE, transmissonModeMapping, TRANSMISSION_MODE_AUTO);
            feparams->u.ofdm.hierarchy_information = MapYamlNode(doc, TAG_HIERARCHY, hierarchyMapping, HIERARCHY_AUTO);
            break;
        case DELSYS_ATSC:
            feparams->u.vsb.modulation = MapYamlNode(doc, TAG_MODULATION, modulationMapping, QAM_AUTO);
            break;
        default:
            break;
    }
}

static void ConvertFEParamsToYaml(DVBDeliverySystem_e delSys, struct dvb_frontend_parameters *feparams, DVBSatelliteSettings_t *satSettings, yaml_document_t *doc)
{
    char temp[25];
    sprintf(temp, "%u", feparams->frequency);
    YamlUtils_MappingAdd(doc, 1, TAG_FREQUENCY, temp);
    YamlUtils_MappingAdd(doc, 1, TAG_INVERSION, MapValueToString(inversionMapping, feparams->inversion, "AUTO"));
    
    switch (delSys)
    {
        case DELSYS_DVBS:
            YamlUtils_MappingAdd(doc, 1, TAG_FEC, MapValueToString(fecMapping, feparams->u.qpsk.fec_inner, "AUTO"));
            sprintf(temp, "%u", feparams->u.qpsk.symbol_rate);
            YamlUtils_MappingAdd(doc, 1, TAG_SYMBOL_RATE, temp); 
            YamlUtils_MappingAdd(doc, 1, TAG_POLARISATION, MapValueToString(polarisationMapping, satSettings->polarisation, "Horizontal"));
            sprintf(temp, "%u", satSettings->satellite_number);
            YamlUtils_MappingAdd(doc, 1, TAG_SATELLITE_NUMBER, temp); 
            break;
        case DELSYS_DVBC:
            YamlUtils_MappingAdd(doc, 1, TAG_FEC, MapValueToString(fecMapping, feparams->u.qam.fec_inner, "AUTO"));
            sprintf(temp, "%u", feparams->u.qam.symbol_rate);
            YamlUtils_MappingAdd(doc, 1, TAG_SYMBOL_RATE, temp); 
            YamlUtils_MappingAdd(doc, 1, TAG_MODULATION, MapValueToString(modulationMapping, feparams->u.qam.modulation, "AUTO"));
            break;
        case DELSYS_DVBT:
            YamlUtils_MappingAdd(doc, 1, TAG_BANDWIDTH, MapValueToString(bandwidthMapping, feparams->u.ofdm.bandwidth, "AUTO"));
            YamlUtils_MappingAdd(doc, 1, TAG_FEC_HP, MapValueToString(fecMapping, feparams->u.ofdm.code_rate_HP, "AUTO"));
            YamlUtils_MappingAdd(doc, 1, TAG_FEC_LP, MapValueToString(fecMapping, feparams->u.ofdm.code_rate_LP, "AUTO"));
            YamlUtils_MappingAdd(doc, 1, TAG_CONSTELLATION, MapValueToString(modulationMapping, feparams->u.ofdm.constellation, "AUTO"));
            YamlUtils_MappingAdd(doc, 1, TAG_GUARD_INTERVAL,MapValueToString(guardIntervalMapping, feparams->u.ofdm.guard_interval, "AUTO"));
            YamlUtils_MappingAdd(doc, 1, TAG_TRANSMISSION_MODE, MapValueToString(transmissonModeMapping, feparams->u.ofdm.transmission_mode, "AUTO"));
            YamlUtils_MappingAdd(doc, 1, TAG_HIERARCHY, MapValueToString(hierarchyMapping, feparams->u.ofdm.hierarchy_information, "AUTO"));
            break;
        case DELSYS_ATSC:
            YamlUtils_MappingAdd(doc, 1, TAG_MODULATION, MapValueToString(modulationMapping, feparams->u.vsb.modulation, "AUTO"));
            break;
        default:
            break;
    }
}

#else 
static uint32_t ConvertStringToBandwith(const char *str, uint32_t defaultValue)
{
    char *suffix;
    uint32_t result = strtoul(str, &suffix, 10);
    if (suffix == str)
    {
        result = defaultValue;
    }
    else if (*suffix)
    {
        if (strcasecmp(suffix, "Mhz") == 0)
        {
            result *= 1000000;
        }
        else
        {
            result = defaultValue;
        }
    }
    return result;
}

static int ConvertYamlToDTVProperties(DVBDeliverySystem_e delSys, yaml_document_t *doc, struct dtv_properties *feparams)
{
    
}

static void ConvertDTVPropertiesToYaml(DVBDeliverySystem_e delSys, struct dtv_properties *feparams, yaml_document_t *doc)
{
    
}

#endif

