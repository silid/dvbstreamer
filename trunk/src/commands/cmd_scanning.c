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

scanning.c

Command functions related to scanning multiplex and frequency bands.

*/
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include <getopt.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <linux/dvb/frontend.h>

#include "commands.h"
#include "multiplexes.h"
#include "services.h"
#include "dvbadapter.h"
#include "ts.h"
#include "logging.h"
#include "cache.h"
#include "main.h"
#include "deliverymethod.h"
#include "plugin.h"
#include "servicefilter.h"
#include "tuning.h"
#include "patprocessor.h"
#include "pmtprocessor.h"
#include "sdtprocessor.h"
#include "nitprocessor.h"
#include "psipprocessor.h"
#include "events.h"
#include "properties.h"
#include "yamlutils.h"
#include "dispatchers.h"
#include "dvbpsi/dr.h"

/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/
struct PMTReceived_t
{
    uint16_t id;
    uint16_t pid;
    bool received;
};

typedef struct TransponderEntry_s
{
    int netId;
    int tsId;
    unsigned int frequency;
    char *polarisation;
    DVBDeliverySystem_e delSys;
    char *tuningParams;
    struct TransponderEntry_s *next;
}TransponderEntry_t;

typedef struct TuningParamDocs_s
{
    int nrofDocs;
    char *docs[0];
}TuningParamDocs_t;

typedef struct ScanEntry_s
{
    DVBDeliverySystem_e system;
    Multiplex_t *mux;
    TuningParamDocs_t *params;
    struct ScanEntry_s *next;
}ScanEntry_t;

typedef struct ScanList_s
{
    ScanEntry_t *start;
    ScanEntry_t *end;
    ScanEntry_t *current;
    int count;
    int pos;
}ScanList_t;

typedef struct MuxFrequency_s
{
    unsigned int frequency;
    char polarisation[11];
    int satNumber;
}MuxFrequency_t;

typedef struct MuxFrequencies_s
{
    int nrofFrequencies;
    MuxFrequency_t frequencies[0];
}MuxFrequencies_t;

enum ScanType_e {
    ScanType_List,
    ScanType_Network
};

enum ScanEvent_e
{
    ScanEvent_NoEvent,
    ScanEvent_StateEntered,
    ScanEvent_FELocked,
    ScanEvent_NextTuningParams,
    ScanEvent_PATReceived,
    ScanEvent_PMTsReceived,
    ScanEvent_SDTReceived,
    ScanEvent_Cancel,
    ScanEvent_TimerTick,
};

enum ScanState_e
{
    ScanState_Init,
    ScanState_NextMux,
    ScanState_WaitingForTables,
    ScanState_WaitingForNIT,
    ScanState_Stopping,
    ScanState_Stopped,
    ScanState_Canceling,
};

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void CommandScan(int argc, char **argv);
static void CommandScanCancel(int argc, char **argv);

static void ScanCurrentMultiplexes(void);

#if defined(ENABLE_DVB)
static void ScanFullDVBT(void);
static void ScanFullDVBC(void);
static void TryTuneDVBC(MultiplexList_t *muxList, MuxFrequencies_t *muxFreqList, 
                             unsigned int freq, char *inversion, char *code_rate,
                            __u32 *symbolRates, int nrofSymbolRates, 
                            char **modulations, int nrofModulations);

static void SDTEventListener(void *arg, Event_t event, void *payload);
static void NITEventListener(void *arg, Event_t event, void *payload);
static bool FindTransponder(int freq, char *polarisation);
static double BCDFixedPoint3_7ToDouble(uint32_t bcd);
#endif

#if defined(ENABLE_ATSC)
static void ScanFullATSC(void);
static void VCTEventListener(void *arg, Event_t event, void *payload);
#endif

static void ScanNetwork(char *initialdata);

static void PATEventListener(void *arg, Event_t event, void *payload);
static void PMTEventListener(void *arg, Event_t event, void *payload);

static void FELockedEventListener(void *arg, Event_t event, void *payload);
static void ScanEntryDestructor(void *ptr);
static void TuningParamDocsDestructor(void *ptr);
static void TransponderEntryDestructor(void *ptr);

static MuxFrequencies_t *ParseMuxListFrequencies(MultiplexList_t *muxList);
static Multiplex_t *FindMultiplexFrequency(MultiplexList_t *muxList, MuxFrequencies_t *muxFreqList, unsigned long freq, int range, char *polarisation, int satNumber);

static void ScanStartStopWatcher(struct ev_loop *loop, ev_async *w, int revents);
static void TimeoutWatcher(struct ev_loop * loop,ev_timer * w,int revents);


static void ScanStart(enum ScanType_e scanType);
static void ScanStop(void);
static void ScanStateMachine(enum ScanEvent_e event);
static void ScanListReset(void);
static void ScanListAddEntry(DVBDeliverySystem_e delSys, Multiplex_t *mux, TuningParamDocs_t *docs);
static ScanEntry_t *ScanListNextEntry(void);

static int ScanEventToString(yaml_document_t *document, Event_t event, void *payload);
static int ScanningInProgressGet(void *userArg, PropertyValue_t *value);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
Command_t CommandDetailsScanning[] =
{
    {
        "scan",
        1,2,
        "Scan the specified multiplex(es) for services.",
        "scan <multiplex>\n"
        "Tunes to the specified multiplex and waits for up to 5 seconds to acquire service information.\n"
        "scan all\n"
        "Tunes to all known multiplexes and waits for up to 5 seconds on each multiplex to acquire service information.\n"
        "scan full\n"
        "Performs a full spectrum scan looking for available multiplexes. This method only supports DVB-T/C and ATSC.\n"
        "scan network <initial tuning data>\n"
        "Performs a network scan using the initial tuning data provided this is in the same format as supplied in the initial tuning data files supplied with the dvb-utils from linuxtv.org.\n"
        "The tuning data should be quoted, ie\n"
        "scan network \"T 489833000 8MHz 3/4 NONE QAM16 2k 1/32 NONE\"\n"
        "Networks can be scan by calling this command more than once with different initial tuning data.\n",
        CommandScan
    },
    {
        "cancelscan",
        0, 0,
        "Cancel the any scan that is in progress.",
        "cancelscan\n"
        "Cancels any scans that are currently in progress.",
        CommandScanCancel
    },
    COMMANDS_SENTINEL
};


static char SCANNING[]="Scanning";
static char propertyParent[] = "commands.scan";

static bool cancelScan = FALSE;
static Service_t *currentService;

static char *AUTO = "AUTO";
static char *NONE = "NONE";

#if defined(ENABLE_DVB)
static char *bandwidthTable[] = {"8000000", "7000000", "6000000", "AUTO", "AUTO", "AUTO", "AUTO", "AUTO"};
static char *ofdmCodeRateTable[] = {"1/2", "2/3", "3/4", "4/5","5/6", "7/8", "NONE", "NONE"};
static char *ofdmConstellationTable[] = {"QPSK", "16QAM", "64QAM", "AUTO"};
static char *ofdmHierarchyTable[] = {"NONE", "1", "2", "4", "NONE", "1", "2", "4"};
static char *ofdmGuardIntTable[] = {"1/32", "1/16", "1/8", "1/4"};
static char *ofdmTransmitModeTable[] = { "2000", "8000", "AUTO", "AUTO"};
static char *fecInnerTable[] = {"NONE", "1/2", "2/3", "3/4", "5/6", "7/8", "8/9", "AUTO",
                                "4/5", "AUTO", "NONE", "NONE", "NONE", "NONE", "NONE", "NONE"};
static char* polarisationTable[] = {"Horizontal", "Vertical", "Left", "Right"};
static char* rollOffTable[] = {"0.35", "0.25", "0.20", ""};

#endif

static ScanList_t toScan = {NULL, NULL, NULL, 0, 0};

static List_t *transponderList = NULL;
static int PMTCount = 0;
static struct PMTReceived_t *PMTsReceived = NULL;
static pthread_mutex_t scanningmutex = PTHREAD_MUTEX_INITIALIZER;

static int lockTimeout = 30;
static int tablesTimeout = 15;
static bool removeFailedFreqs = TRUE;

#if defined(ENABLE_DVB)
/* DVB-T Related variables */
static bool DVBTScanVHF = TRUE;
static bool DVBTScanUHF = TRUE;

/* DVB-S Related variables */
static int DVBSSatNumber = 0;
#endif

#if defined(ENABLE_ATSC)
/* ATSC Related variables */
static bool ATSCScanOTA = TRUE;
static bool ATSCScanCable = TRUE;
#endif

int deliverySystemRanges[] = {
     0, /* DELSYS_DVBS  */
     0, /* DELSYS_DVBC  */
166670, /* DELSYS_DVBT  */    
 28615, /* DELSYS_ATSC  */    
     0, /* DELSYS_DVBS2 */    
};

static enum ScanType_e scanType = ScanType_List;
static enum ScanState_e currentScanState = ScanState_Stopped, previousScanState = ScanState_Stopped;

static ev_async scanStartAsync;
static ev_timer timeoutTimer;

static EventSource_t scanEventSource;
static Event_t scanStartEvent;
static Event_t scanEndEvent;
static Event_t scanCanceledEvent;
static Event_t scanTryingMuxEvent;
static Event_t scanMuxAddedEvent;

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
void CommandInstallScanning(void)
{
    struct ev_loop *loop;
    Event_t feLockedEvent;
    char propertyName[PROPERTIES_PATH_MAX];
    LogModule(LOG_DEBUG,SCANNING,"Starting to install scanning.\n");

    ObjectRegisterTypeDestructor(ScanEntry_t, ScanEntryDestructor);
    ObjectRegisterTypeDestructor(TransponderEntry_t, TransponderEntryDestructor);
    ObjectRegisterCollection(TOSTRING(TuningParamDocs_t),sizeof(char *),TuningParamDocsDestructor);
    ObjectRegisterCollection(TOSTRING(MuxFrequencies_t), sizeof(MuxFrequency_t), NULL);

    scanEventSource = EventsRegisterSource("scan");
    scanStartEvent = EventsRegisterEvent(scanEventSource, "started", ScanEventToString); /* TODO Add ToString func */
    scanEndEvent = EventsRegisterEvent(scanEventSource, "finished", NULL); /* TODO Add ToString func */    
    scanCanceledEvent = EventsRegisterEvent(scanEventSource, "cancel", NULL); /* TODO Add ToString func */    
    scanTryingMuxEvent = EventsRegisterEvent(scanEventSource, "trying", ScanEventToString); /* TODO Add ToString func */    
    scanMuxAddedEvent = EventsRegisterEvent(scanEventSource, "found", NULL); /* TODO Add ToString func */    
    
    loop = DispatchersGetInput();
    ev_async_init(&scanStartAsync, ScanStartStopWatcher);
    ev_timer_init(&timeoutTimer, TimeoutWatcher, 1.0, 1.0);
    ev_async_start(loop, &scanStartAsync);
#if defined(ENABLE_DVB)
    if (MainIsDVB())
    {
        EventsRegisterEventListener(EventsFindEvent("dvb.sdt"),SDTEventListener,NULL);
        EventsRegisterEventListener(EventsFindEvent("dvb.nit"),NITEventListener, NULL);

    }
#endif    

#if defined(ENABLE_ATSC)
    if (MainIsATSC())
    {
        EventsRegisterEventListener(EventsFindEvent("atsc.vct"),VCTEventListener, NULL);        
    }
#endif

    EventsRegisterEventListener(EventsFindEvent("mpeg2.pat"),PATEventListener, NULL);
    EventsRegisterEventListener(EventsFindEvent("mpeg2.pmt"),PMTEventListener, NULL);

    LogModule(LOG_DEBUG,SCANNING,"Finding fe locked event.\n");
    feLockedEvent = EventsFindEvent("DVBAdapter.Locked");
    EventsRegisterEventListener(feLockedEvent, FELockedEventListener, NULL);

    PropertiesAddProperty(propertyParent, "inprogress","Whether an scan is currently in progress", PropertyType_Boolean, NULL , ScanningInProgressGet, NULL);
    PropertiesAddSimpleProperty(propertyParent, "state", "Get the current state id of the scanning state machine.",
        PropertyType_Int, &currentScanState, SIMPLEPROPERTY_R);
    
    PropertiesAddSimpleProperty(propertyParent, "removefailed", "Whether frequencies currently in the database that fail to lock should be removed.",
        PropertyType_Boolean, &removeFailedFreqs, SIMPLEPROPERTY_RW);

    PropertiesAddSimpleProperty(propertyParent, "locktimeout", "Number of seconds to wait for the frontent to lock.",
        PropertyType_Int, &lockTimeout, SIMPLEPROPERTY_RW);
    PropertiesAddSimpleProperty(propertyParent, "tablestimeout", "Number of seconds to wait for the required tables.",
        PropertyType_Int, &tablesTimeout, SIMPLEPROPERTY_RW);
    
#if defined(ENABLE_DVB)
    /* DVB-T Properties */
    sprintf(propertyName, "%s.dvb.t", propertyParent);
    PropertiesAddSimpleProperty(propertyName, "scanvhf", "Whether VHF channels should be scanned when doing a full spectrum scan",
        PropertyType_Boolean, &DVBTScanVHF, SIMPLEPROPERTY_RW);
    PropertiesAddSimpleProperty(propertyName, "scanuhf", "Whether UHF channels should be scanned when doing a full spectrum scan",
        PropertyType_Boolean, &DVBTScanUHF, SIMPLEPROPERTY_RW);

    /* DVB-S Properties */
    sprintf(propertyName, "%s.dvb.s", propertyParent);
    PropertiesAddSimpleProperty(propertyName, "scansatnumber", "The switch position/satellite number to scan.",
        PropertyType_Int, &DVBSSatNumber, SIMPLEPROPERTY_RW);
#endif
    /* ATSC Properties */
#if defined(ENABLE_ATSC)
    sprintf(propertyName, "%s.atsc", propertyParent);
    PropertiesAddSimpleProperty(propertyName, "scanota", "Whether OTA ATSC signals should be scanned for.",
        PropertyType_Boolean, &ATSCScanOTA, SIMPLEPROPERTY_RW);
    PropertiesAddSimpleProperty(propertyName, "scancable", "Whether ATSC cable signals should be scanned for.",
        PropertyType_Boolean, &ATSCScanOTA, SIMPLEPROPERTY_RW);
#endif

    CommandRegisterCommands(CommandDetailsScanning);
}

void CommandUnInstallScanning(void)
{
    struct ev_loop *loop = DispatchersGetInput();
    ev_async_stop(loop, &scanStartAsync);
    
    CommandUnRegisterCommands(CommandDetailsScanning);
    PropertiesRemoveAllProperties(propertyParent);
    EventsUnregisterEventListener(EventsFindEvent("DVBAdapter.Locked"),FELockedEventListener, NULL);
    EventsUnregisterEventListener(EventsFindEvent("mpeg2.pat"),PATEventListener, NULL);
    EventsUnregisterEventListener(EventsFindEvent("mpeg2.pmt"),PMTEventListener, NULL);
#if defined(ENABLE_DVB)
    if (MainIsDVB())
    {
        EventsUnregisterEventListener(EventsFindEvent("dvb.sdt"),SDTEventListener, NULL);
        EventsUnregisterEventListener(EventsFindEvent("dvb.nit"),NITEventListener, NULL);
    }
#endif

#if defined(ENABLE_ATSC)
    if (MainIsATSC())
    {
        EventsUnregisterEventListener(EventsFindEvent("atsc.vct"),VCTEventListener, NULL);
    }
#endif

}
/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/

static void CommandScan(int argc, char **argv)
{
    
    Multiplex_t *multiplex;
    int i;
    DVBAdapter_t *adapter;
    DVBSupportedDeliverySys_t *supportedDelSys;
    
    CommandCheckAuthenticated();
    pthread_mutex_lock(&scanningmutex);
    if (currentScanState != ScanState_Stopped)
    {
        CommandError(COMMAND_ERROR_GENERIC,"Scan in progress!");
    }
    else if(TuningCurrentServiceIsLocked())
    {
        CommandError(COMMAND_ERROR_GENERIC,"Current Service is locked!");
    }
    else
    {
        if (strcmp(argv[0], "all") == 0)
        {
            ScanCurrentMultiplexes();
        }
        else if (strcmp(argv[0], "full") == 0)
        {
            adapter = MainDVBAdapterGet();
            supportedDelSys = DVBFrontEndGetDeliverySystems(adapter);
            for (i = 0; i < supportedDelSys->nrofSystems; i ++)
            {
                switch(supportedDelSys->systems[i])
                {
#if defined(ENABLE_DVB)
                    case DELSYS_DVBT:
                        ScanFullDVBT();
                        break;
                    case DELSYS_DVBC:
                        ScanFullDVBC();
                        break;
#endif
#if defined(ENABLE_ATSC)
                    case DELSYS_ATSC:
                        ScanFullATSC();
                        break;
#endif
                    default:
                        CommandError(COMMAND_ERROR_GENERIC, "Frontend type doesn't support a full spectrum scan mode!");
                        break;
                }
            }
            ScanStart(ScanType_List);
        }
        else if (strcmp(argv[0], "net") == 0)
        {
            if (argc == 2)
            {
                ScanNetwork(argv[1]);
            }
            else
            {
                CommandError(COMMAND_ERROR_WRONG_ARGS, "Expected quoted initial tuning data!");
            }
        }
        else
        {
            multiplex = MultiplexFind(argv[0]);
            if (multiplex)
            {
                CommandPrintf("Scanning %d\n", multiplex->uid);
                ScanListReset();
                ScanListAddEntry(multiplex->deliverySystem, multiplex, NULL);
                ScanStart(ScanType_List);
            }
        }
    }
    pthread_mutex_unlock(&scanningmutex);
}

static void CommandScanCancel(int argc, char **argv)
{
    ScanStop();
}
/************************** Scan Callback Functions **************************/

static void ScanCurrentMultiplexes(void)
{
    int i;
    MultiplexList_t *list;
    list = MultiplexGetAll();
    if (list)
    {
        for (i = 0; (i < list->nrofMultiplexes) && !ExitProgram; i ++)
        {
            ScanListAddEntry(list->multiplexes[i]->deliverySystem, list->multiplexes[i], NULL);
            ObjectRefInc(list->multiplexes[i]);
        }
        ObjectRefDec(list);
    }
    ScanStart(ScanType_List);
}

#if defined(ENABLE_DVB)
static void ScanFullDVBT(void)
{
    char *inversion;
    char *constellation;
    char *transmit_mode;
    char *guard_interval;
    char *hierarchy;
    char *code_rate;
    DVBAdapter_t *adapter = MainDVBAdapterGet();
    int channel;
    int channelCount = 0;
    int totalChannels = 0;
    MultiplexList_t *muxList = MultiplexGetAll();
    MuxFrequencies_t *muxFreqList = ParseMuxListFrequencies(muxList);
    Multiplex_t *mux;
    int offsetIndex = 0;
    unsigned int frequency;
    unsigned int offsets[] = {-166670, 0, 166670};

    if (DVBFrontEndParameterSupported(adapter, DELSYS_DVBT, "Inversion", AUTO))
    {
        inversion = AUTO;
    }
    else
    {
        LogModule(LOG_INFO, SCANNING, "INVERSION_AUTO not supported, trying INVERSION_OFF.\n");
        inversion = "OFF";
    }

    if (DVBFrontEndParameterSupported(adapter, DELSYS_DVBT, "Modulation", AUTO))
    {
        constellation = AUTO;
    }
    else
    {
        LogModule(LOG_INFO, SCANNING, "QAM_AUTO not supported, trying QAM_64.\n");
        constellation = "QAM64";
    }

    if (DVBFrontEndParameterSupported(adapter, DELSYS_DVBT, "Transmission Mode", AUTO))
    {
        transmit_mode = AUTO;
    }
    else
    {
        LogModule(LOG_INFO, SCANNING, "TRANSMISSION_MODE not supported, trying TRANSMISSION_MODE_8K.\n");
        transmit_mode = "8K";
    }

    if (DVBFrontEndParameterSupported(adapter, DELSYS_DVBT, "Guard Interval", AUTO))
    {
        guard_interval = AUTO;
    }
    else
    {
        LogModule(LOG_INFO, SCANNING, "GUARD_INTERVAL_AUTO not supported, trying GUARD_INTERVAL_1_8.\n");
        guard_interval = "1/8";
    }

    if (DVBFrontEndParameterSupported(adapter, DELSYS_DVBT, "Hierarchy", AUTO))
    {
        hierarchy = AUTO;
    }
    else
    {
        LogModule(LOG_INFO, SCANNING, "HIERARCHY_AUTO not supported, trying HIERARCHY_NONE.\n");
        hierarchy = NONE;
    }

    if (DVBFrontEndParameterSupported(adapter, DELSYS_DVBT, "FEC", AUTO))
    {
        code_rate = AUTO;
    }
    else
    {
        LogModule(LOG_INFO, SCANNING, "FEC_AUTO not supported, trying FEC_NONE.\n");
        code_rate = NONE;
    }

    if (DVBTScanVHF)
    {
        totalChannels += 8;
    }

    if (DVBTScanUHF)
    {
        totalChannels += 49;
    }

    CommandPrintf("Scanning %d frequencies\n", totalChannels);
    if (DVBTScanVHF)
    {
        for (channel = 5; (channel <= 12) && !cancelScan; channel++)
        {
            frequency = 142500000 + (channel * 7000000);
            channelCount ++;
            for (offsetIndex = 0; offsetIndex < 3; offsetIndex ++)
            {
                mux = FindMultiplexFrequency(muxList, muxFreqList, frequency + offsets[offsetIndex], 0, NULL, 0);
                if (mux)
                {
                    break;
                }
            }
            if (mux)
            {
                ScanListAddEntry(DELSYS_DVBT, mux, NULL);
            }
            else
            {
                TuningParamDocs_t *docs = (TuningParamDocs_t*)ObjectCollectionCreate(TOSTRING(TuningParamDocs_t), 3);
                for (offsetIndex = 0; offsetIndex < 3; offsetIndex ++)
                {
                    asprintf(&docs->docs[offsetIndex], "Frequency: %d\n"
                                                       "Inversion: %s\n"
                                                       "Bandwidth: 7Mhz\n"
                                                       "FEC LP: %s\n"
                                                       "FEC HP: %s\n"
                                                       "Constellation: %s\n"
                                                       "Transmission Mode: %s\n"
                                                       "Guard Interval: %s\n"
                                                       "Hierarchy: %s\n",
                                                        frequency + offsets[offsetIndex],
                                                        inversion,
                                                        code_rate,
                                                        code_rate,
                                                        constellation,
                                                        transmit_mode,
                                                        guard_interval,
                                                        hierarchy);                    
                }
                ScanListAddEntry(DELSYS_DVBT, NULL, docs);
            }

        }
    }

    if (DVBTScanUHF)
    {
        for (channel = 21; (channel <= 69) && !cancelScan; channel++)
        {
            frequency = 306000000 + (channel * 8000000);
            channelCount ++;
            for (offsetIndex = 0; offsetIndex < 3; offsetIndex ++)
            {
                mux = FindMultiplexFrequency(muxList, muxFreqList, frequency + offsets[offsetIndex], 0, NULL, 0);
                if (mux)
                {
                    break;
                }
            }
            if (mux)
            {
                ScanListAddEntry(DELSYS_DVBT, mux, NULL);
            }
            else
            {
                TuningParamDocs_t *docs = (TuningParamDocs_t*)ObjectCollectionCreate(TOSTRING(TuningParamDocs_t), 3);
                for (offsetIndex = 0; offsetIndex < 3; offsetIndex ++)
                {
                    asprintf(&docs->docs[offsetIndex], "Frequency: %d\n"
                                                       "Inversion: %s\n"
                                                       "Bandwidth: 8Mhz\n"
                                                       "FEC LP: %s\n"
                                                       "FEC HP: %s\n"
                                                       "Constellation: %s\n"
                                                       "Transmission Mode: %s\n"
                                                       "Guard Interval: %s\n"
                                                       "Hierarchy: %s\n",
                                                        frequency + offsets[offsetIndex],
                                                        inversion,
                                                        code_rate,
                                                        code_rate,
                                                        constellation,
                                                        transmit_mode,
                                                        guard_interval,
                                                        hierarchy);                    
                }
                ScanListAddEntry(DELSYS_DVBT, NULL, docs);
            }
        }
    }
    ObjectRefDec(muxList);
    ObjectRefDec(muxFreqList);
}

static void ScanFullDVBC(void)
{
    int channel;
    char *inversion;
    #define MAX_MODULATIONS 6
    char *modulations[MAX_MODULATIONS];
    int nrofModulations = 0;
    __u32 symbolRates[] = { 6900000, 6875000};
    int nrofSymbolRates = 2;
    char *code_rate;
    DVBAdapter_t *adapter = MainDVBAdapterGet();
    MultiplexList_t *muxList = MultiplexGetAll();
    MuxFrequencies_t *muxFreqList = ParseMuxListFrequencies(muxList);    

    if (DVBFrontEndParameterSupported(adapter, DELSYS_DVBC, "Inversion", AUTO))
    {
        inversion = AUTO;
    }
    else
    {
        LogModule(LOG_INFO, SCANNING, "INVERSION_AUTO not supported, trying INVERSION_OFF.\n");
        inversion = "OFF";
    }

    if (DVBFrontEndParameterSupported(adapter, DELSYS_DVBC, "Modulation", AUTO))
    {
        modulations[0] = AUTO;
        nrofModulations = 1;
    }
    else
    {
        LogModule(LOG_INFO, SCANNING, "QAM_AUTO not supported, trying QAM_64.\n");
        modulations[0] = "QAM64";
        modulations[1] = "QAM128";
        modulations[2] = "QAM256";
        nrofModulations = 3;
    }
     if (DVBFrontEndParameterSupported(adapter, DELSYS_DVBC, "FEC", AUTO))
    {
        code_rate = AUTO;
    }
    else
    {
        LogModule(LOG_INFO, SCANNING, "FEC_AUTO not supported, trying FEC_NONE.\n");
        code_rate = NONE;
    }

    for (channel=2; (channel <= 4) && !cancelScan; channel++)
    {
        TryTuneDVBC(muxList, muxFreqList, 36500000 + (channel * 7000000), inversion, code_rate, symbolRates, nrofSymbolRates, modulations, nrofModulations);
    }

    for (channel=1; (channel <= 10) && !cancelScan; channel++)
    {
        TryTuneDVBC(muxList, muxFreqList, 100500000 + (channel * 7000000), inversion, code_rate, symbolRates, nrofSymbolRates, modulations, nrofModulations);
    }

    for (channel=1; (channel <= 9) && !cancelScan; channel++)
    {
        TryTuneDVBC(muxList, muxFreqList, 97000000 + (channel * 8000000), inversion, code_rate, symbolRates, nrofSymbolRates, modulations, nrofModulations);
    }

    for (channel=5; (channel <= 22) && !cancelScan; channel++)
    {
        TryTuneDVBC(muxList, muxFreqList, 142500000 + (channel * 7000000), inversion, code_rate, symbolRates, nrofSymbolRates, modulations, nrofModulations);
    }

    for (channel=21; (channel <= 90) && !cancelScan; channel++)
    {
        TryTuneDVBC(muxList, muxFreqList, 138000000 + (channel * 8000000), inversion, code_rate, symbolRates, nrofSymbolRates, modulations, nrofModulations);
    }
    ObjectRefDec(muxList);
    ObjectRefDec(muxFreqList);
}

static void TryTuneDVBC(MultiplexList_t *muxList, MuxFrequencies_t *muxFreqList, 
                             unsigned int freq, char *inversion, char *code_rate,
                            __u32 *symbolRates, int nrofSymbolRates, 
                            char **modulations, int nrofModulations)
{
    Multiplex_t *mux;
    TuningParamDocs_t *docs;
    int s, m;
    mux = FindMultiplexFrequency(muxList, muxFreqList, freq, 0, NULL, 0);
    if (mux)
    {
        ScanListAddEntry(DELSYS_DVBC, mux, NULL);
    }
    else
    {
        docs = (TuningParamDocs_t *)ObjectCollectionCreate(TOSTRING(TuningParamDocs_t), nrofSymbolRates * nrofModulations);
        for (s = 0; s < nrofSymbolRates; s ++)
        {
            for (m = 0; m < nrofModulations; m ++)
            {
                asprintf(&docs->docs[s*m], "Frequency: %u\n"
                                           "Inversion: %s\n"
                                           "FEC: %s\n"
                                           "Symbol Rate: %u\n"
                                           "Modulation: %s\n",
                                           freq,
                                           inversion,
                                           code_rate,
                                           symbolRates[s],
                                           modulations[m]);
                ScanListAddEntry(DELSYS_DVBC, NULL, docs);
            }
        }

    }
    

}
#endif

#if defined(ENABLE_ATSC)
static void ScanFullATSC(void)
{
    MultiplexList_t *muxList = MultiplexGetAll();
    MuxFrequencies_t *muxFreqList = ParseMuxListFrequencies(muxList);
    Multiplex_t *mux;
    int channel;
    int base_offset = 0;
    unsigned int freq;
    
    if (ATSCScanOTA)
    {
        for (channel = 2; (channel <= 69) && !cancelScan; channel++)
        {
            

            if (channel < 5)
            {
                base_offset = 45028615;
            }
            else if (channel < 7)
            {
                base_offset = 49028615;
            }
            else if (channel < 14)
            {
                base_offset = 135028615;
            }
            else
            {
                base_offset = 389028615;
            }
            freq = base_offset + (channel * 6000000);
            
            mux = FindMultiplexFrequency(muxList, muxFreqList, freq, 28615, NULL, 0);
            if (mux)
            {
                ScanListAddEntry(DELSYS_ATSC, mux, NULL);
            }
            else
            {
                TuningParamDocs_t *docs = (TuningParamDocs_t *)ObjectCollectionCreate(TOSTRING(TuningParamDocs_t), 1);
                asprintf(&docs->docs[0], "Frequency: %u\n"
                                         "Inversion: AUTO\n"
                                         "Modulation: 8VSB\n",
                                         freq);
                ScanListAddEntry(DELSYS_ATSC, NULL, docs);
            }
        }
    }
    if (ATSCScanCable)
    {
        for (channel = 2; (channel <= 133) && !cancelScan; channel++)
        {
            if (channel < 5)
                base_offset = 45000000;
            else if (channel < 7)
                base_offset = 49000000;
            else if (channel < 14)
                base_offset = 135000000;
            else if (channel < 17)
                base_offset = 39012500;
            else if (channel < 23)
                base_offset = 39000000;
            else if (channel < 25)
                base_offset = 81000000;
            else if (channel < 54)
                base_offset = 81012500;
            else if (channel < 95)
                base_offset = 81000000;
            else if (channel < 98)
                base_offset = -477000000;
            else if (channel < 100)
                base_offset = -476987500;
            else
                base_offset = 51000000;

            freq = base_offset + (channel * 6000000);

            mux = FindMultiplexFrequency(muxList, muxFreqList, freq, 0, NULL, 0);
            if (mux)
            {
                ScanListAddEntry(DELSYS_ATSC, mux, NULL);
            }
            else
            {
                TuningParamDocs_t *docs = (TuningParamDocs_t *)ObjectCollectionCreate(TOSTRING(TuningParamDocs_t), 1);
                asprintf(&docs->docs[0], "Frequency: %u\n"
                                         "Inversion: AUTO\n"
                                         "Modulation: QAM256\n",
                                         freq);
                ScanListAddEntry(DELSYS_ATSC, NULL, docs);
            }
        } 
    }
    ObjectRefDec(muxList);
    ObjectRefDec(muxFreqList);
}
#endif
static void ScanNetwork(char *initialdata)
{
    Multiplex_t *mux;
    bool parsed = FALSE;
    char modStr[7];
#if defined(ENABLE_DVB)
    __u32 symbolRate;
    char bwStr[5];
    char fecHiStr[5];
    char fecLoStr[5];
    char transModeStr[5];
    char guardIntStr[5];
    char hierarchyStr[5];
    char polarisationStr[2];
#endif
    char params[256];
    char *polarisation = NULL;
    char *inversion;
    DVBDeliverySystem_e delSys;
    unsigned int frequency;
    MultiplexList_t *muxList = MultiplexGetAll();
    MuxFrequencies_t *muxFreqList = ParseMuxListFrequencies(muxList);
    DVBAdapter_t *adapter = MainDVBAdapterGet();
    int muxFindRange = 0;

    /* Initial Tuning data formats:
     *
     * DVB-T
     * T freq bw fec_hi fec_lo mod transmission-mode guard-interval hierarchy
     *
     * DVB-C
     * C freq sr fec mod
     *
     * DVB-S
     * S freq pol sr fec
     *
     * ATSC
     * A freq mod
     */
    switch (initialdata[0])
    {
        case 'T': delSys = DELSYS_DVBT; break;
        case 'S': delSys = DELSYS_DVBS; break;
        case 'C': delSys = DELSYS_DVBC; break;
        case 'A': delSys = DELSYS_ATSC; break;
    }
    
    if (!DVBFrontEndDeliverySystemSupported(adapter, delSys))
    {
        CommandError(COMMAND_ERROR_GENERIC, "Frontend doesn't support the required delivery system!");
        return;
    }
    
    if (DVBFrontEndParameterSupported(adapter, delSys, "Inversion", AUTO))
    {
        inversion = AUTO;
    }
    else
    {
        LogModule(LOG_INFO, SCANNING, "INVERSION_AUTO not supported, trying INVERSION_OFF.\n");
        inversion = "OFF";
    }
    switch (delSys)
    {
#if defined(ENABLE_DVB)
        case DELSYS_DVBT:
            
            if (sscanf(initialdata, "T %u %4s %4s %4s %6s %4s %4s %4s",
                    &frequency, bwStr, fecHiStr, fecLoStr, modStr, transModeStr, guardIntStr, hierarchyStr) == 8)
            {
                sprintf(params, "Frequency: %u\n"
                                "Inversion: %s\n"
                                "Bandwidth: %s\n"
                                "FEC HP: %s\n"
                                "FEC LP: %s\n"
                                "Constellation: %s\n"
                                "Transmission Mode: %s\n"
                                "Guard Interval: %s\n"
                                "Hierarchy: %s\n",
                                frequency,
                                inversion,
                                bwStr,
                                fecHiStr,
                                fecLoStr,
                                modStr,
                                transModeStr,
                                guardIntStr,
                                hierarchyStr);
                muxFindRange = 166670;
                parsed = TRUE;
            }
            break;

        case DELSYS_DVBC:
            if (sscanf(initialdata, "C %u %u %4s %6s", &frequency, &symbolRate, fecHiStr, modStr) == 4)
            {
                sprintf(params, "Frequency: %u\n"
                                "Inversion: %s\n"   
                                "Symbol Rate: %u\n"
                                "FEC: %s\n"
                                "Modulation: %s\n",
                                frequency, inversion, symbolRate, fecHiStr, modStr);
                muxFindRange = 0;
                parsed = TRUE;
            }
            break;

        case DELSYS_DVBS:
            if (sscanf(initialdata, "S %u %1[HVLR] %u %4s\n", &frequency, polarisationStr, &symbolRate, fecHiStr) == 4)
            {
                switch(polarisationStr[0])
                {
                    case 'L':
                        polarisation = "Left";
                        break;
                    case 'R':
                        polarisation = "Right";
                        break;
                    case 'H':
                        polarisation = "Horizontal";
                        break;
                    case 'V':
                    default: /* fall-through */
                        polarisation = "Vertical";
                        break;
                }
                sprintf(params, "Frequency: %u\n"
                                "Inversion: %s\n"
                                "Symbol Rate: %u\n"
                                "FEC: %s\n"
                                "Polarisation: %s\n"
                                "Satellite Number: %d\n",
                                frequency, inversion, symbolRate,
                                fecHiStr, polarisation, DVBSSatNumber);
                muxFindRange = 0;
                parsed = TRUE;
            }
            break;
#endif

#if defined(ENABLE_ATSC)
        case DELSYS_ATSC:
            if (sscanf(initialdata, "A %u %7s\n", &frequency, modStr) == 2)
            {
                sprintf(params, "Frequency: %u\n"
                                "Inversion: %s\n"
                                "Modulation: %s\n",
                                frequency,
                                inversion,
                                modStr);
                muxFindRange = 28615;
                parsed = TRUE;
            }
            break;
#endif
        default:
            break;
    }

    if (parsed)
    {
        transponderList = ListCreate();
        mux = FindMultiplexFrequency(muxList, muxFreqList, frequency, muxFindRange, polarisation, DVBSSatNumber);
        if (mux)
        {
            ScanListAddEntry(delSys, mux, NULL);
        }
        else
        {
            TuningParamDocs_t *docs = (TuningParamDocs_t *)ObjectCollectionCreate(TOSTRING(TuningParamDocs_t), 1);
            docs->docs[0] = strdup(params);
            ScanListAddEntry(delSys, NULL, docs);
        }
        ScanStart(ScanType_Network);
    }
    else
    {
        CommandError(COMMAND_ERROR_GENERIC, "Failed to parse initial tuning data!");
    }
}

static void PATEventListener(void *arg, Event_t event, void *payload)
{
    dvbpsi_pat_t* newpat = payload;
    if (currentScanState == ScanState_WaitingForTables)
    {
        int i;
        dvbpsi_pat_program_t *patentry = newpat->p_first_program;
        TSReader_t *tsFilter = MainTSReaderGet();
        PMTCount = 0;
        while(patentry)
        {
            if (patentry->i_number != 0x0000)
            {
                PMTCount ++;
            }
            patentry = patentry->p_next;
        }
        if (PMTsReceived)
        {
            ObjectFree(PMTsReceived);
        }
        PMTsReceived = ObjectAlloc(sizeof(struct PMTReceived_t) * PMTCount);
        patentry = newpat->p_first_program;
        i = 0;
        while(patentry)
        {
            if (patentry->i_number != 0x0000)
            {
                PMTsReceived[i].id = patentry->i_number;
                PMTsReceived[i].pid = patentry->i_pid;
                i ++;
            }
            patentry = patentry->p_next;
        }
        tsFilter->tsStructureChanged = TRUE; /* Force all PMTs to be received again incase we are scanning a mux we have pids for */

        ScanStateMachine(ScanEvent_PATReceived);        
    }
}

static void PMTEventListener(void *arg, Event_t event, void *payload)
{
    dvbpsi_pmt_t *newpmt = payload;
    if (currentScanState == ScanState_WaitingForTables)
    {
        bool all = TRUE;
        int i;
        for (i = 0; i < PMTCount; i ++)
        {
            if (PMTsReceived[i].id == newpmt->i_program_number)
            {
                PMTsReceived[i].received = TRUE;
            }
        }

        for (i = 0; i < PMTCount; i ++)
        {
            if (!PMTsReceived[i].received)
            {
                all = FALSE;
            }
        }

        if (all)
        {
            ScanStateMachine(ScanEvent_PMTsReceived);
        }
    }
}
#if defined(ENABLE_DVB)
static void SDTEventListener(void *arg, Event_t event, void *payload)
{
    if (currentScanState == ScanState_WaitingForTables)
    {
        ScanStateMachine(ScanEvent_SDTReceived);
    }
}

static void NITEventListener(void *arg, Event_t event, void *payload)
{
    dvbpsi_nit_t* newnit = payload;
    DVBAdapter_t *adapter = MainDVBAdapterGet();
    TransponderEntry_t *tpEntry;
    int i;
    dvbpsi_nit_transport_t *transport = NULL;
    unsigned int frequency;
    char tparams[256];
    char sparams[256];    
    char *polarisation = NULL;
    DVBDeliverySystem_e delSys;
    tparams[0] = 0;
    sparams[0] = 0;
    
#define ADD_TRANSPONDER(_params) \
    do {\
        if (!FindTransponder(frequency, polarisation)) \
        {\
            tpEntry = ObjectCreateType(TransponderEntry_t);\
            if (tpEntry != NULL)\
            {\
                tpEntry->delSys = delSys;\
                asprintf(&tpEntry->tuningParams, "Frequency: %u\n%s", frequency, _params);\
                tpEntry->frequency = frequency;\
                tpEntry->netId = transport->i_original_network_id;\
                tpEntry->tsId = transport->i_ts_id;\
                ListAdd(transponderList, tpEntry);\
            }\
        }\
    }while(FALSE)

    if (((currentScanState == ScanState_WaitingForTables) ||
        (currentScanState == ScanState_WaitingForNIT)) &&
        (scanType == ScanType_Network) &&
        (transponderList != NULL))
    {
        for (transport = newnit->p_first_transport; transport; transport = transport->p_next)
        {
            dvbpsi_descriptor_t *descriptor;
            for (descriptor = transport->p_first_descriptor; descriptor; descriptor = descriptor->p_next)
            {
                switch(descriptor->i_tag)
                {
                    case 0x43:
                        sparams[0] = 0;
                        dvbpsi_sat_deliv_sys_dr_t *satDelSysDr = dvbpsi_DecodeSatDelivSysDr(descriptor);
                        polarisation = polarisationTable[satDelSysDr->i_polarization];
                        if (DVBFrontEndDeliverySystemSupported(adapter, DELSYS_DVBS) && 
                            (satDelSysDr->i_modulation_system != 1))
                        {

                            double freq = BCDFixedPoint3_7ToDouble(satDelSysDr->i_frequency);
                            double symbolRate = BCDFixedPoint3_7ToDouble(satDelSysDr->i_symbol_rate << 4);

                            frequency = (unsigned int)(freq * 1000000.0);
                            sprintf(tparams, "Inversion: AUTO\n"
                                             "FEC: %s\n"
                                             "Symbol Rate: %u\n"
                                             "Polarisation: %s\n"
                                             "Satellite Number: %d\n",
                                             fecInnerTable[satDelSysDr->i_fec_inner],
                                             (unsigned int)(symbolRate * 1000000.0),
                                             polarisation,
                                             DVBSSatNumber);
                            delSys = DELSYS_DVBS;
                            ADD_TRANSPONDER(sparams);
                        }
                        if (DVBFrontEndDeliverySystemSupported(adapter, DELSYS_DVBS2) && 
                            (satDelSysDr->i_modulation_system == 1))
                        {

                            double freq = BCDFixedPoint3_7ToDouble(satDelSysDr->i_frequency);
                            double symbolRate = BCDFixedPoint3_7ToDouble(satDelSysDr->i_symbol_rate << 4);

                            frequency = (unsigned int)(freq * 1000000.0);
                            sprintf(tparams, "Inversion: AUTO\n"
                                             "FEC: %s\n"
                                             "Symbol Rate: %u\n"
                                             "Roll Off: %s\n"
                                             "Polarisation: %s\n"
                                             "Satellite Number: %d\n",
                                             fecInnerTable[satDelSysDr->i_fec_inner],
                                             (unsigned int)(symbolRate * 1000000.0),
                                             rollOffTable[satDelSysDr->i_roll_off],
                                             polarisationTable[satDelSysDr->i_polarization],
                                             DVBSSatNumber);
                            delSys = DELSYS_DVBS2;
                            ADD_TRANSPONDER(sparams);
                        }
                        break;
                    case 0x5a:
                        polarisation = NULL;
                        tparams[0] = 0;
                        if (DVBFrontEndDeliverySystemSupported(adapter, DELSYS_DVBT))
                        {
                            dvbpsi_terr_deliv_sys_dr_t *terrDelSysDr = dvbpsi_DecodeTerrDelivSysDr(descriptor);
                            if (terrDelSysDr)
                            {
                                frequency = terrDelSysDr->i_centre_frequency * 10;
                                sprintf(tparams, "Inversion: AUTO\n"
                                                 "Bandwidth: %s\n"
                                                 "FEC HP: %s\n"
                                                 "FEC LP: %s\n"
                                                 "Constellation: %s\n"
                                                 "Guard Interval: %s\n"
                                                 "Hierarchy: %s\n"
                                                 "Transmission Mode: %s\n",
                                                 bandwidthTable[terrDelSysDr->i_bandwidth],
                                                 ofdmCodeRateTable[terrDelSysDr->i_code_rate_hp_stream],
                                                 ofdmCodeRateTable[terrDelSysDr->i_code_rate_lp_stream],
                                                 ofdmConstellationTable[terrDelSysDr->i_constellation],
                                                 ofdmGuardIntTable[terrDelSysDr->i_guard_interval],
                                                 ofdmHierarchyTable[terrDelSysDr->i_hierarchy_information],
                                                 ofdmTransmitModeTable[terrDelSysDr->i_transmission_mode]);
                                
                                delSys = DELSYS_DVBT;
                                ADD_TRANSPONDER(tparams);
                                if (terrDelSysDr->i_other_frequency_flag == 0)
                                {
                                    tparams[0] = 0;
                                }
                            }
                        }
                        break;
                        
                    case 0x62:
                        {
                            dvbpsi_frequency_list_dr_t *freqListDr = dvbpsi_DecodeFrequencyListDr(descriptor);
                            switch(freqListDr->i_coding_type)
                            {
                                case 1:
                                    if (sparams[0])
                                    {
                                        for (i = 0; i < freqListDr->i_number_of_frequencies; i ++)
                                        {
                                            double freq = BCDFixedPoint3_7ToDouble(freqListDr->p_center_frequencies[i]);
                                            frequency =  (unsigned int)(freq * 1000000.0);
                                            ADD_TRANSPONDER(sparams);
                                        }
                                    }
                                    break;
                                case 2:
                                    break;
                                case 3:
                                    if (tparams[0])
                                    {
                                        for (i = 0; i < freqListDr->i_number_of_frequencies; i ++)
                                        {
                                            frequency = freqListDr->p_center_frequencies[i] * 10;
                                            ADD_TRANSPONDER(tparams);
                                        }
                                    }
                                    break;
                            }
                        }
                        break;
                }
            }
        }
    }
}

static bool FindTransponder(int freq, char *polarisation)
{
    ListIterator_t iterator;
    for (ListIterator_Init(iterator, transponderList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        TransponderEntry_t *entry = (TransponderEntry_t *)ListIterator_Current(iterator);
        if (entry->frequency == freq)
        {
            if (polarisation && entry->polarisation && (strcmp(entry->polarisation, polarisation) == 0))
            {
                return TRUE;
            }
        }
    }
    return FALSE;
}

static double BCDFixedPoint3_7ToDouble(uint32_t bcd)
{
    int integer;
    int fraction;
    integer = (((bcd >> 28) & 0xf) * 100) +
              (((bcd >> 24) & 0xf) * 10)  +
              (((bcd >> 20) & 0xf));
    fraction = (((bcd >> 16) & 0xf) * 10000) +
               (((bcd >> 12) & 0xf) * 1000)  +
               (((bcd >>  8) & 0xf) * 100)   +
               (((bcd >>  4) & 0xf) * 10)    +
               (((bcd      ) & 0xf));

    return (double)integer + ((double)fraction / 100000.0);
}
#endif

#if defined(ENABLE_ATSC)
static void VCTEventListener(void *arg, Event_t event, void *payload)
{
    if (currentScanState == ScanState_WaitingForTables)
    {
        ScanStateMachine(ScanEvent_SDTReceived);
    }
}
#endif

static void FELockedEventListener(void *arg, Event_t event, void *payload)
{
    if (currentScanState == ScanState_NextMux)
    {
        ScanStateMachine(ScanEvent_FELocked);
    }
}

static MuxFrequencies_t *ParseMuxListFrequencies(MultiplexList_t *muxList)
{
    int i;
    MuxFrequencies_t *result = (MuxFrequencies_t *)ObjectCollectionCreate(TOSTRING(MuxFrequencies_t), muxList->nrofMultiplexes);
    if (result)
    {
        for (i = 0;i < muxList->nrofMultiplexes; i ++)
        {
            yaml_document_t document;
            memset(&document, 0, sizeof(document));
            if (YamlUtils_Parse(muxList->multiplexes[i]->tuningParams, &document))
            {
                yaml_node_t *node = YamlUtils_RootMappingFind(&document, "Frequency");
                if (node && (node->type == YAML_SCALAR_NODE))
                {
                    result->frequencies[i].frequency = strtoul((const char *)node->data.scalar.value, NULL, 10);
                }
                if ((muxList->multiplexes[i]->deliverySystem == DELSYS_DVBS) ||
                    (muxList->multiplexes[i]->deliverySystem == DELSYS_DVBS2)) 
                {
                    node = YamlUtils_RootMappingFind(&document, "Polarisation");
                    if (node && (node->type == YAML_SCALAR_NODE))
                    {
                        strncpy(result->frequencies[i].polarisation, (const char *)node->data.scalar.value, sizeof(result->frequencies[i].polarisation));
                    }
                    node = YamlUtils_RootMappingFind(&document, "Satellite Number");
                    if (node && (node->type == YAML_SCALAR_NODE))
                    {
                        result->frequencies[i].satNumber = atoi((const char *)node->data.scalar.value);
                    }

                }
            }
            yaml_document_delete(&document);
        }
    }
    return result;
}

static Multiplex_t *FindMultiplexFrequency(MultiplexList_t *muxList, MuxFrequencies_t *muxFreqList, 
                                                unsigned long freq, int range, 
                                                char *polarisation, int satNumber)
{
    int i;
    for (i = 0; i < muxList->nrofMultiplexes; i ++)
    {
        if ((muxFreqList->frequencies[i].frequency >= freq - range) &&
            (muxFreqList->frequencies[i].frequency <= freq + range))
        {
            bool found = TRUE;
            if (polarisation)
            {
                found = (muxFreqList->frequencies[i].polarisation &&
                         (strcmp(polarisation, muxFreqList->frequencies[i].polarisation) == 0) &&
                           (muxFreqList->frequencies[i].satNumber == satNumber));

            }
            if (found)
            {
                ObjectRefInc(muxList->multiplexes[i]);
                return muxList->multiplexes[i];
            }
        }
    }
    return NULL;
}

static void ProcessTransponderList(void)
{
    Multiplex_t *mux;
    ListIterator_t iterator;
    MultiplexList_t *muxList = MultiplexGetAll();
    MuxFrequencies_t *muxFreqList = ParseMuxListFrequencies(muxList);
    
    if (ListCount(transponderList) > 0)
    {
        for (ListIterator_Init(iterator, transponderList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
        {
            TransponderEntry_t *entry = (TransponderEntry_t*)ListIterator_Current(iterator);
            mux = MultiplexFindId(entry->netId, entry->tsId);
            if (mux)
            {
                /* Skip as we already have this network/ts combo */
                MultiplexRefDec(mux);
            }
            else
            {
                mux = FindMultiplexFrequency(muxList, muxFreqList, 
                                             entry->frequency, deliverySystemRanges[entry->delSys],
                                             entry->polarisation, DVBSSatNumber);
                if (mux)
                {
                    ScanListAddEntry(entry->delSys, mux, NULL);
                }
                else
                {
                    TuningParamDocs_t *docs = (TuningParamDocs_t *)ObjectCollectionCreate(TOSTRING(TuningParamDocs_t), 1);
                    docs->docs[0] = entry->tuningParams;
                    entry->tuningParams = NULL;
                    ScanListAddEntry(entry->delSys, NULL, docs);
                }
            }
        }
    }
    ObjectRefDec(muxList);
    ObjectRefDec(muxFreqList);
}

static void ScanStart(enum ScanType_e type)
{
    scanType = type;
    cancelScan = FALSE;
    ev_async_send(DispatchersGetInput(), &scanStartAsync);
}

static void ScanStop(void)
{
    cancelScan = TRUE;
    ev_async_send(DispatchersGetInput(), &scanStartAsync);
}

static void ScanStartStopWatcher(struct ev_loop *loop, ev_async *w, int revents)
{
    if (cancelScan)
    {
        ScanStateMachine(ScanEvent_Cancel);
    }
    else
    {
        currentScanState = ScanState_Init;
        previousScanState = currentScanState;
        ScanStateMachine(ScanEvent_StateEntered);
    }
}

static void TimeoutWatcher(struct ev_loop *loop, ev_timer *w, int revents)
{
    ScanStateMachine(ScanEvent_TimerTick);
    ev_timer_again(loop, w);
}

static void ScanStateMachine(enum ScanEvent_e event)
{
    static bool PATReceived = FALSE;
    static bool PMTReceived = FALSE;
    static bool SDTReceived = FALSE;
    static ScanEntry_t *currentEntry = NULL;
    static int currentTuningParams = 0;
    static int timeout = 0;
    enum ScanEvent_e nextEvent;
    DVBDeliverySystem_e delSys;
    char *tuningParams;
    LogModule(LOG_INFOV, SCANNING, "ScanStateMachine: event = %d", event);
    for (; event!= ScanEvent_NoEvent; event = nextEvent)
    {
        nextEvent = ScanEvent_NoEvent;
        previousScanState = currentScanState;
        switch (currentScanState)
        {
            case ScanState_Init:
                if (event == ScanEvent_StateEntered)
                {
                    EventsFireEventListeners(scanStartEvent,NULL);
                    ev_timer_start(DispatchersGetInput(), &timeoutTimer); 
                    currentService = TuningCurrentServiceGet();
                    TuningCurrentServiceLock();
                    toScan.current = toScan.start;
                    currentScanState = ScanState_NextMux;
                }
                break;
            case ScanState_NextMux:
                switch (event)
                {
                    case ScanEvent_StateEntered:
                        
                        EventsFireEventListeners(scanTryingMuxEvent,NULL);
                        currentTuningParams = -1;
                        currentEntry = ScanListNextEntry();
                        if (currentEntry)
                        {
                            nextEvent = ScanEvent_NextTuningParams;
                        }
                        else
                        {
                            currentScanState = ScanState_Stopping;
                        }
                        break;
                    case ScanEvent_NextTuningParams:
                        timeout = lockTimeout;
                        currentTuningParams ++;
                        tuningParams = NULL;
                        if (currentEntry->mux)
                        {
                            if (currentTuningParams == 0)
                            {
                                delSys = currentEntry->mux->deliverySystem;
                                tuningParams = currentEntry->mux->tuningParams;
                            }
                        }
                        else
                        {
                            if (currentTuningParams < currentEntry->params->nrofDocs)
                            {
                                delSys = currentEntry->system;
                                tuningParams = currentEntry->params->docs[currentTuningParams];
                            }
                        }
                        if (tuningParams)
                        {
                            TSReader_t *tsReader = MainTSReaderGet();
                            DVBAdapter_t *adapter = MainDVBAdapterGet();
                            TSReaderEnable(tsReader, FALSE);
                            DVBFrontEndTune(adapter, delSys, tuningParams);
                        }
                        else
                        {
                            nextEvent = ScanEvent_StateEntered;
                        }
                        break;
                        
                    case ScanEvent_FELocked:
                        if (currentEntry->mux == NULL)
                        {
                            DVBAdapter_t *adapter = MainDVBAdapterGet();
                            DVBDeliverySystem_e delSys;
                            char *tuningParams = DVBFrontEndParametersGet(adapter,  &delSys);
                            MultiplexAdd(delSys, tuningParams, &currentEntry->mux);
                            free(tuningParams);
                            EventsFireEventListeners(scanMuxAddedEvent,NULL);
                        }
                        TuningCurrentMultiplexSet(currentEntry->mux);
                        currentScanState = ScanState_WaitingForTables;
                        break;
                    case ScanEvent_TimerTick:                        
                        timeout --;
                        if (timeout <= 0)
                        {
                            nextEvent = ScanEvent_NextTuningParams;
                            if (removeFailedFreqs && currentEntry->mux)
                            {
                                MultiplexDelete(currentEntry->mux);
                            }
                        }
                        break;
                    default:
                        break;
                }
                break;
            case ScanState_WaitingForTables:
                switch (event)
                {
                    case ScanEvent_StateEntered:
                        PATReceived = FALSE;
                        PMTReceived = FALSE;
                        SDTReceived = FALSE;
                        timeout = tablesTimeout;
                        break;

                    case ScanEvent_PATReceived:
                        PATReceived = TRUE;
                        break;

                    case ScanEvent_PMTsReceived:
                        PMTReceived = TRUE;
                        break;

                    case ScanEvent_SDTReceived:
                        SDTReceived = TRUE;
                        break;
                    case ScanEvent_TimerTick:
                        timeout --;
                        if (timeout <= 0)
                        {
                            currentScanState = ScanState_NextMux;
                        }
                        break;
                    default:
                        break;
                }
                if (PATReceived && PMTReceived && SDTReceived)
                {
#if defined(ENABLE_DVB)
                    if ((scanType == ScanType_Network) && (MainIsDVB()))
                    {
                        currentScanState = ScanState_WaitingForNIT;
                    }
                    else
#endif
                    {
                        currentScanState = ScanState_NextMux;
                    }
                }
                break;
            case ScanState_WaitingForNIT:
                switch (event)
                {
                    case ScanEvent_StateEntered:
                        timeout = tablesTimeout;
                        break;
                    case ScanEvent_TimerTick:
                        timeout --;
                        if (timeout <= 0)
                        {
                            ProcessTransponderList();
                            currentScanState = ScanState_NextMux;
                        }
                        break;
                    default:
                        break;                        
                }
                break;
                
            case ScanState_Canceling:
                if (event == ScanEvent_StateEntered)
                {
                    EventsFireEventListeners(scanCanceledEvent, NULL);
                    currentScanState = ScanState_Stopping;
                }
                break;

            case ScanState_Stopping: 
                if (event == ScanEvent_StateEntered)
                {
                    TuningCurrentServiceUnlock();
                    TuningCurrentServiceRetune();
                    if (PMTsReceived)
                    {
                        ObjectFree(PMTsReceived);
                        PMTsReceived = NULL;
                    }
                    if (transponderList)
                    {
                        ObjectListFree(transponderList);
                        transponderList = NULL;
                    }
                    currentScanState = ScanState_Stopped;
                }   
                break;
            case ScanState_Stopped:
                if (event == ScanEvent_StateEntered)
                {
                    EventsFireEventListeners(scanEndEvent, NULL);
                }
                break;
        }
        
        if (event == ScanEvent_Cancel)
        {
            currentScanState = ScanState_Canceling;
        }
            
        if (currentScanState != previousScanState)
        {
            LogModule(LOG_INFOV,SCANNING,"Previous State (%d) != Current State(%d)", previousScanState, currentScanState);
            nextEvent = ScanEvent_StateEntered;
        }
        LogModule(LOG_INFOV,SCANNING, "State %d Next Event = %d", currentScanState, nextEvent);
    }
    
}

static void ScanListReset(void)
{
    ScanEntry_t *entry;
    for (entry = toScan.start; entry; entry = entry->next)
    {
        ObjectRefDec(entry);
    }
    toScan.start = NULL;
    toScan.end = NULL;
    toScan.current = NULL;
    toScan.count = 0;
    toScan.pos = 0;
}


static void ScanListAddEntry(DVBDeliverySystem_e delSys, Multiplex_t *mux, TuningParamDocs_t *docs)
{
    ScanEntry_t *entry;
    entry = ObjectCreateType(ScanEntry_t);
    entry->system = delSys;
    entry->mux = mux;
    entry->params = docs;
    if (toScan.start == NULL)
    {
        toScan.start = entry;
        toScan.current = entry;
    }
    else
    {
        toScan.end->next = entry;
    }
    toScan.end = entry;
    toScan.count ++;

}

static ScanEntry_t *ScanListNextEntry(void)
{
    ScanEntry_t *entry = NULL;
    if (toScan.current)
    {
        entry = toScan.current;
        toScan.current = entry->next;
        toScan.pos ++;
    }
    return entry;
}

static void ScanEntryDestructor(void *ptr)
{
    ScanEntry_t *entry = ptr;
    if (entry->mux)
    {
        ObjectRefDec(entry->mux);
    }
    
    if (entry->params)
    {
        ObjectRefDec(entry->params);
    }
}

static void TransponderEntryDestructor(void *ptr)
{
    TransponderEntry_t *entry = ptr;
    if (entry->tuningParams)
    {
        free(entry->tuningParams);
    }
}

static void TuningParamDocsDestructor(void *ptr)
{
    TuningParamDocs_t *docs = ptr;
    int i;

    for (i = 0; i < docs->nrofDocs; i ++)
    {
        if (docs->docs[i])
        {
            free(docs->docs[i]);
        }
    }    
}

static int ScanEventToString(yaml_document_t *document, Event_t event, void *payload)
{
    char temp[5];
    int mappingId = yaml_document_add_mapping(document, (yaml_char_t*)YAML_MAP_TAG, YAML_ANY_MAPPING_STYLE);
    //if (event == scanStartEvent)
    {
        sprintf(temp, "%d", toScan.count);
        YamlUtils_MappingAdd(document, mappingId, "Total transponders", temp);
    }
    //if (event == scanTryingMuxEvent)
    {
        sprintf(temp, "%d", toScan.pos);
        YamlUtils_MappingAdd(document, mappingId, "Transponder", temp);
    }
    LogModule(LOG_INFO,SCANNING,"Total %d Current %d", toScan.count, toScan.pos);
    return mappingId;
}

static int ScanningInProgressGet(void *userArg, PropertyValue_t *value)
{
    value->type = PropertyType_Boolean;
    value->u.boolean = (currentScanState != ScanState_Stopped);
    return 0;
}

