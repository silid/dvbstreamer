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
    struct dvb_frontend_parameters feparams;
    DVBDiSEqCSettings_t diseqc;
}TransponderEntry_t;

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void CommandScan(int argc, char **argv);
static void CommandScanCancel(int argc, char **argv);
static void ScanCurrentMultiplexes(void);

#if defined(ENABLE_DVB)
static void ScanFullDVBT(void);
static void ScanFullDVBC(void);
static void TryTuneDVBC(int channelCount, struct dvb_frontend_parameters *feparams,
   __u32 *symbolRates, int nrofSymbolRates, fe_modulation_t *modulations, int nrofModulations);
static void SDTCallback(dvbpsi_sdt_t* newsdt);
static void NITCallback(dvbpsi_nit_t* newnit);
static bool FindTransponder(int freq);
static double BCDFixedPoint3_7ToDouble(uint32_t bcd);
#endif

#if defined(ENABLE_ATSC)
static void ScanFullATSC(void);
static void VCTCallback(dvbpsi_atsc_vct_t* newvct);
#endif

static void ScanNetwork(char *initialdata);
static void ScanMultiplex(Multiplex_t *multiplex, bool needNIT);
static int TuneFrequency(fe_type_t type, struct dvb_frontend_parameters *feparams, DVBDiSEqCSettings_t * diseqc, Multiplex_t *mux, bool needNIT);
static void PATCallback(dvbpsi_pat_t* newpat);
static void PMTCallback(dvbpsi_pmt_t* newpmt);

static void FELockedEventListener(void *arg, Event_t event, void *payload);
/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
Command_t CommandDetailsScanning[] =
{
    {
        "scan",
        TRUE, 1,2,
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
        FALSE, 0, 0,
        "Cancel the any scan that is in progress.",
        "cancelscan\n"
        "Cancels any scans that are currently in progress.",
        CommandScanCancel
    },
    {NULL, FALSE, 0, 0, NULL, NULL, NULL}
};


static char SCANNING[]="Scanning";
static char propertyParent[] = "commands.scan";
static bool scanning = FALSE;
static bool cancelScan = FALSE;
static bool PATReceived = FALSE;
static bool AllPMTReceived = FALSE;
static bool SDTReceived = FALSE;

#if defined(ENABLE_DVB)
static bool NITneeded = FALSE;
#endif

static List_t *transponderList = NULL;
static bool waitingForFELocked= FALSE;
static bool FELocked = FALSE;
static int PMTCount = 0;
static int PMTNextIndex = 0;
static struct PMTReceived_t *PMTsReceived = NULL;
static pthread_mutex_t scanningmutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t scanningcond = PTHREAD_COND_INITIALIZER;

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

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
void CommandInstallScanning(void)
{
    Event_t feLockedEvent;
    char propertyName[PROPERTIES_PATH_MAX];
    LogModule(LOG_DEBUG,SCANNING,"Starting to install scanning.\n");
    if (MainIsDVB())
    {
#if defined(ENABLE_DVB)
        SDTProcessorRegisterSDTCallback(SDTCallback);
        NITProcessorRegisterNITCallback(NITCallback);
#endif
    }
    else
    {
#if defined(ENABLE_ATSC)
        PSIPProcessorRegisterVCTCallback(VCTCallback);
#endif
    }
    PATProcessorRegisterPATCallback(PATCallback);
    PMTProcessorRegisterPMTCallback(PMTCallback);
    LogModule(LOG_DEBUG,SCANNING,"Finding fe locked event.\n");
    feLockedEvent = EventsFindEvent("DVBAdapter.Locked");
    EventsRegisterEventListener(feLockedEvent, FELockedEventListener, NULL);

    PropertiesAddProperty(propertyParent, "removefailed", "Whether frequencies currently in the database that fail to lock should be removed.",
        PropertyType_Boolean, &removeFailedFreqs, PropertiesSimplePropertyGet, PropertiesSimplePropertySet);
#if defined(ENABLE_DVB)
    /* DVB-T Properties */
    sprintf(propertyName, "%s.dvb.t", propertyParent);
    PropertiesAddProperty(propertyName, "scanvhf", "Whether VHF channels should be scanned when doing a full spectrum scan",
        PropertyType_Boolean, &DVBTScanVHF, PropertiesSimplePropertyGet, PropertiesSimplePropertySet);
    PropertiesAddProperty(propertyName, "scanuhf", "Whether UHF channels should be scanned when doing a full spectrum scan",
        PropertyType_Boolean, &DVBTScanUHF, PropertiesSimplePropertyGet, PropertiesSimplePropertySet);

    /* DVB-S Properties */
    sprintf(propertyName, "%s.dvb.s", propertyParent);
    PropertiesAddProperty(propertyName, "scansatnumber", "The switch position/satellite number to scan.",
        PropertyType_Int, &DVBSSatNumber, PropertiesSimplePropertyGet, PropertiesSimplePropertySet);
#endif
    /* ATSC Properties */
#if defined(ENABLE_ATSC)
    sprintf(propertyName, "%s.atsc", propertyParent);
    PropertiesAddProperty(propertyName, "scanota", "Whether OTA ATSC signals should be scanned for.",
        PropertyType_Boolean, &ATSCScanOTA, PropertiesSimplePropertyGet, PropertiesSimplePropertySet);
    PropertiesAddProperty(propertyName, "scancable", "Whether ATSC cable signals should be scanned for.",
        PropertyType_Boolean, &ATSCScanOTA, PropertiesSimplePropertyGet, PropertiesSimplePropertySet);
#endif

    CommandRegisterCommands(CommandDetailsScanning);
}

void CommandUnInstallScanning(void)
{
    CommandUnRegisterCommands(CommandDetailsScanning);
    PropertiesRemoveAllProperties(propertyParent);
    scanning = FALSE;
    PATProcessorUnRegisterPATCallback(PATCallback);
    PMTProcessorUnRegisterPMTCallback(PMTCallback);
    if (MainIsDVB())
    {
#if defined(ENABLE_DVB)
        SDTProcessorUnRegisterSDTCallback(SDTCallback);
        NITProcessorUnRegisterNITCallback(NITCallback);
#endif
    }
    else
    {
#if defined(ENABLE_ATSC)
        PSIPProcessorUnRegisterVCTCallback(VCTCallback);
#endif
    }
}
/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/

static void CommandScan(int argc, char **argv)
{
    Service_t *currentService;
    Multiplex_t *multiplex;

    DVBAdapter_t *adapter;

    CommandCheckAuthenticated();
    currentService = TuningCurrentServiceGet();
    cancelScan = FALSE;

    if (strcmp(argv[0], "all") == 0)
    {
        ScanCurrentMultiplexes();
    }
    else if (strcmp(argv[0], "full") == 0)
    {
        adapter = MainDVBAdapterGet();
        switch(adapter->info.type)
        {
#if defined(ENABLE_DVB)
            case FE_OFDM:
                ScanFullDVBT();
                break;
            case FE_QAM:
                ScanFullDVBC();
                break;
#endif
#if defined(ENABLE_ATSC)
            case FE_ATSC:
                ScanFullATSC();
                break;
#endif
            default:
                CommandError(COMMAND_ERROR_GENERIC, "Frontend type doesn't support a full spectrum scan mode!");
                break;
        }
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
            ScanMultiplex(multiplex, FALSE);
            MultiplexRefDec(multiplex);
        }
    }

    if (currentService)
    {
        TuningCurrentServiceSet(currentService);
        ServiceRefDec(currentService);
    }

    if (cancelScan)
    {
        CommandError(COMMAND_ERROR_GENERIC, "Scan cancelled");
    }
}

static void CommandScanCancel(int argc, char **argv)
{
    cancelScan = TRUE;
    pthread_mutex_lock(&scanningmutex);
    pthread_cond_signal(&scanningcond);
    pthread_mutex_unlock(&scanningmutex);
}
/************************** Scan Callback Functions **************************/

static void ScanCurrentMultiplexes(void)
{
    Multiplex_t *multiplex;
    int count = MultiplexCount();
    Multiplex_t **multiplexes = ObjectAlloc(count * sizeof(Multiplex_t *));
    if (multiplexes)
    {
        int i =0;
        MultiplexEnumerator_t enumerator = MultiplexEnumeratorGet();
        do
        {
            multiplex = MultiplexGetNext(enumerator);
            if (multiplex)
            {
                multiplexes[i] = multiplex;
                i ++;
            }
        }while(multiplex && ! ExitProgram);
        MultiplexEnumeratorDestroy(enumerator);

        for (i = 0; (i < count) && ! ExitProgram; i ++)
        {
            CommandPrintf("Scanning %d\n", multiplexes[i]->uid);
            ScanMultiplex(multiplexes[i], FALSE);
            MultiplexRefDec(multiplexes[i]);
        }

        ObjectFree(multiplexes);
    }
}
#if defined(ENABLE_DVB)
static void ScanFullDVBT(void)
{
    fe_spectral_inversion_t inversion;
    fe_modulation_t modulation;
    fe_transmit_mode_t transmit_mode;
    fe_guard_interval_t guard_interval;
    fe_hierarchy_t hierarchy;
    fe_code_rate_t code_rate;
    struct dvb_frontend_parameters feparams;
    DVBAdapter_t *adapter = MainDVBAdapterGet();
    int channel;
    int channelCount = 0;
    int totalChannels = 0;
    Multiplex_t *mux;
    int offsetIndex = 0;
    unsigned int frequency;
    unsigned int offsets[] = {-166670, 0, 166670};

    if (adapter->info.caps & FE_CAN_INVERSION_AUTO)
    {
        inversion = INVERSION_AUTO;
    }
    else
    {
        LogModule(LOG_INFO, SCANNING, "INVERSION_AUTO not supported, trying INVERSION_OFF.\n");
        inversion = INVERSION_OFF;
    }

    if (adapter->info.caps & FE_CAN_QAM_AUTO)
    {
        modulation = QAM_AUTO;
    }
    else
    {
        LogModule(LOG_INFO, SCANNING, "QAM_AUTO not supported, trying QAM_64.\n");
        modulation = QAM_64;
    }

    if (adapter->info.caps & FE_CAN_TRANSMISSION_MODE_AUTO)
    {
        transmit_mode = TRANSMISSION_MODE_AUTO;
    }
    else
    {
        LogModule(LOG_INFO, SCANNING, "TRANSMISSION_MODE not supported, trying TRANSMISSION_MODE_8K.\n");
        transmit_mode = TRANSMISSION_MODE_8K;
    }

    if (adapter->info.caps & FE_CAN_GUARD_INTERVAL_AUTO)
    {
        guard_interval = GUARD_INTERVAL_AUTO;
    }
    else
    {
        LogModule(LOG_INFO, SCANNING, "GUARD_INTERVAL_AUTO not supported, trying GUARD_INTERVAL_1_8.\n");
        guard_interval = GUARD_INTERVAL_1_8;
    }

    if (adapter->info.caps & FE_CAN_HIERARCHY_AUTO)
    {
        hierarchy = HIERARCHY_AUTO;
    }
    else
    {
        LogModule(LOG_INFO, SCANNING, "HIERARCHY_AUTO not supported, trying HIERARCHY_NONE.\n");
        hierarchy = HIERARCHY_NONE;
    }

    if (adapter->info.caps & FE_CAN_FEC_AUTO)
    {
        code_rate = FEC_AUTO;
    }
    else
    {
        LogModule(LOG_INFO, SCANNING, "FEC_AUTO not supported, trying FEC_NONE.\n");
        code_rate = FEC_NONE;
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
            feparams.inversion = inversion;
            feparams.u.ofdm.bandwidth = BANDWIDTH_7_MHZ;
            feparams.u.ofdm.code_rate_HP = code_rate;
            feparams.u.ofdm.code_rate_LP = code_rate;
            feparams.u.ofdm.constellation = modulation;
            feparams.u.ofdm.transmission_mode = transmit_mode;
            feparams.u.ofdm.guard_interval = guard_interval;
            feparams.u.ofdm.hierarchy_information = hierarchy;
            frequency = 142500000 + (channel * 7000000);
            CommandPrintf("%d %u\n", channelCount + 1, frequency);
            channelCount ++;
            for (offsetIndex = 0; offsetIndex < 3; offsetIndex ++)
            {
                feparams.frequency = frequency + offsets[offsetIndex];
                mux = MultiplexFindFrequency(feparams.frequency);
                TuneFrequency(FE_OFDM, &feparams, NULL, mux, FALSE);
                if (FELocked)
                {
                    break;
                }
            }

        }
    }

    if (DVBTScanUHF)
    {
        for (channel = 21; (channel <= 69) && !cancelScan; channel++)
        {
            feparams.inversion = inversion;
            feparams.u.ofdm.bandwidth = BANDWIDTH_8_MHZ;
            feparams.u.ofdm.code_rate_HP = code_rate;
            feparams.u.ofdm.code_rate_LP = code_rate;
            feparams.u.ofdm.constellation = modulation;
            feparams.u.ofdm.transmission_mode = transmit_mode;
            feparams.u.ofdm.guard_interval = guard_interval;
            feparams.u.ofdm.hierarchy_information = hierarchy;
            frequency = 306000000 + (channel * 8000000);
            CommandPrintf("%d %u\n", channelCount + 1, frequency);
            channelCount ++;

            for (offsetIndex = 0; offsetIndex < 3; offsetIndex ++)
            {
                feparams.frequency = frequency + offsets[offsetIndex];
                mux = MultiplexFindFrequency(feparams.frequency);                
                TuneFrequency(FE_OFDM, &feparams, NULL, mux, FALSE);
                if (FELocked)
                {
                    break;
                }
            }
        }
    }

}

static void ScanFullDVBC(void)
{
    int channel;
    fe_spectral_inversion_t inversion;
    #define MAX_MODULATIONS 6
    fe_modulation_t modulations[MAX_MODULATIONS];
    int nrofModulations = 0;
    __u32 symbolRates[] = { 6900000, 6875000};
    int nrofSymbolRates = 2;
    fe_code_rate_t code_rate;
    struct dvb_frontend_parameters feparams;
    DVBAdapter_t *adapter = MainDVBAdapterGet();
    int channelCount = 0;
    int totalChannels = 0;

    if (adapter->info.caps & FE_CAN_INVERSION_AUTO)
    {
        inversion = INVERSION_AUTO;
    }
    else
    {
        LogModule(LOG_INFO, SCANNING, "INVERSION_AUTO not supported, trying INVERSION_OFF.\n");
        inversion = INVERSION_OFF;
    }

    if (adapter->info.caps & FE_CAN_QAM_AUTO)
    {
        modulations[0] = QAM_AUTO;
        nrofModulations = 1;
    }
    else
    {
        LogModule(LOG_INFO, SCANNING, "QAM_AUTO not supported, trying QAM_64.\n");
        modulations[0] = QAM_64;
        modulations[1] = QAM_128;
        modulations[2] = QAM_256;
        nrofModulations = 3;
    }

    if (adapter->info.caps & FE_CAN_FEC_AUTO)
    {
        code_rate = FEC_AUTO;
    }
    else
    {
        LogModule(LOG_INFO, SCANNING, "FEC_AUTO not supported, trying FEC_NONE.\n");
        code_rate = FEC_NONE;
    }


    feparams.inversion = inversion;
    feparams.u.qam.fec_inner = FEC_NONE;

    totalChannels = 3 + 11 + 10 + 18 + 70;
    CommandPrintf("Scanning %d frequencies\n", totalChannels);

    for (channel=2; (channel <= 4) && !cancelScan; channel++)
    {
        feparams.frequency = 36500000 + (channel * 7000000);
        feparams.inversion = inversion;
        feparams.u.qam.fec_inner = code_rate;
        TryTuneDVBC(channelCount, &feparams, symbolRates, nrofSymbolRates, modulations, nrofModulations);
        channelCount ++;
    }

    for (channel=1; (channel <= 10) && !cancelScan; channel++)
    {
        feparams.frequency = 100500000 + (channel * 7000000);
        feparams.inversion = inversion;
        feparams.u.qam.fec_inner = code_rate;
        TryTuneDVBC(channelCount, &feparams, symbolRates, nrofSymbolRates, modulations, nrofModulations);
        channelCount ++;
    }

    for (channel=1; (channel <= 9) && !cancelScan; channel++)
    {
        feparams.frequency = 97000000 + (channel * 8000000);
        feparams.inversion = inversion;
        feparams.u.qam.fec_inner = code_rate;
        TryTuneDVBC(channelCount, &feparams, symbolRates, nrofSymbolRates, modulations, nrofModulations);
        channelCount ++;
    }

    for (channel=5; (channel <= 22) && !cancelScan; channel++)
    {
        feparams.frequency = 142500000 + (channel * 7000000);
        feparams.inversion = inversion;
        feparams.u.qam.fec_inner = code_rate;
        TryTuneDVBC(channelCount, &feparams, symbolRates, nrofSymbolRates, modulations, nrofModulations);
        channelCount ++;
    }

    for (channel=21; (channel <= 90) && !cancelScan; channel++)
    {
        feparams.frequency = 138000000 + (channel * 8000000);
        feparams.inversion = inversion;
        feparams.u.qam.fec_inner = code_rate;
        TryTuneDVBC(channelCount, &feparams, symbolRates, nrofSymbolRates, modulations, nrofModulations);
        channelCount ++;
    }

}

static void TryTuneDVBC(int channelCount, struct dvb_frontend_parameters *feparams,
   __u32 *symbolRates, int nrofSymbolRates, fe_modulation_t *modulations, int nrofModulations)
{
    Multiplex_t *mux;
    int s, m;
    mux = MultiplexFindFrequency(feparams->frequency);
    MultiplexRefInc(mux);
    CommandPrintf("%d %u\n", channelCount + 1, feparams->frequency);
    for (s = 0; s < nrofSymbolRates; s ++)
    {
        feparams->u.qam.symbol_rate = symbolRates[s];
        for (m = 0; m < nrofModulations; m ++)
        {
            feparams->u.qam.modulation = modulations[m];
            if (TuneFrequency(FE_OFDM, feparams, NULL, mux, FALSE) == 0)
            {
                return;
            }
        }
    }
    MultiplexRefDec(mux);
}
#endif

#if defined(ENABLE_ATSC)
static void ScanFullATSC(void)
{
    Multiplex_t *mux;
    int channel;
    int base_offset = 0;
    struct dvb_frontend_parameters feparams;
    int channelCount = 0;
    int totalChannels = 0;

    if (ATSCScanOTA)
    {
        totalChannels += 68;
    }

    if (ATSCScanCable)
    {
        totalChannels += 132;
    }

    CommandPrintf("Scanning %d frequencies\n", totalChannels);

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

            feparams.u.vsb.modulation = VSB_8;
            feparams.frequency = base_offset + (channel * 6000000);
            CommandPrintf("%d %u\n", channelCount + 1, feparams.frequency);
            channelCount ++;
            mux = MultiplexFindFrequencyRange(feparams.frequency, 28615);
            TuneFrequency(FE_ATSC, &feparams, NULL, mux, FALSE);
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

            feparams.u.vsb.modulation = QAM_256;
            feparams.frequency = base_offset + (channel * 6000000);

            CommandPrintf("%d %u\n", channelCount + 1, feparams.frequency);
            channelCount ++;

            mux = MultiplexFindFrequency(feparams.frequency);
            TuneFrequency(FE_ATSC, &feparams, NULL, mux, FALSE);
    } }
}
#endif
/** Following code shameless copied from  linuxtv dvb-utils (scan.c)*/
struct strtab {
    const char *str;
    int val;
};
static int str2enum(const char *str, const struct strtab *tab, int deflt)
{
    while (tab->str) {
        if (!strcmp(tab->str, str))
            return tab->val;
        tab++;
    }
    LogModule(LOG_ERROR, SCANNING, "Unknown value %s!\n", str);
    return deflt;
}


static enum fe_modulation str2qam(const char *qam)
{
    struct strtab qamtab[] = {
        { "QPSK",   QPSK },
        { "QAM16",  QAM_16 },
        { "QAM32",  QAM_32 },
        { "QAM64",  QAM_64 },
        { "QAM128", QAM_128 },
        { "QAM256", QAM_256 },
        { "AUTO",   QAM_AUTO },
        { "8VSB",   VSB_8 },
        { "16VSB",  VSB_16 },
        { NULL, 0 }
    };
    return str2enum(qam, qamtab, QAM_AUTO);
}
#if defined(ENABLE_DVB)
static enum fe_code_rate str2fec(const char *fec)
{
    struct strtab fectab[] = {
        { "NONE", FEC_NONE },
        { "1/2",  FEC_1_2 },
        { "2/3",  FEC_2_3 },
        { "3/4",  FEC_3_4 },
        { "4/5",  FEC_4_5 },
        { "5/6",  FEC_5_6 },
        { "6/7",  FEC_6_7 },
        { "7/8",  FEC_7_8 },
        { "8/9",  FEC_8_9 },
        { "AUTO", FEC_AUTO },
        { NULL, 0 }
    };
    return str2enum(fec, fectab, FEC_AUTO);
}

static enum fe_bandwidth str2bandwidth(const char *bw)
{
    struct strtab bwtab[] = {
        { "8MHz", BANDWIDTH_8_MHZ },
        { "7MHz", BANDWIDTH_7_MHZ },
        { "6MHz", BANDWIDTH_6_MHZ },
        { "AUTO", BANDWIDTH_AUTO },
        { NULL, 0 }
    };
    return str2enum(bw, bwtab, BANDWIDTH_AUTO);
}

static enum fe_transmit_mode str2mode(const char *mode)
{
    struct strtab modetab[] = {
        { "2k",   TRANSMISSION_MODE_2K },
        { "8k",   TRANSMISSION_MODE_8K },
        { "AUTO", TRANSMISSION_MODE_AUTO },
        { NULL, 0 }
    };
    return str2enum(mode, modetab, TRANSMISSION_MODE_AUTO);
}

static enum fe_guard_interval str2guard(const char *guard)
{
    struct strtab guardtab[] = {
        { "1/32", GUARD_INTERVAL_1_32 },
        { "1/16", GUARD_INTERVAL_1_16 },
        { "1/8",  GUARD_INTERVAL_1_8 },
        { "1/4",  GUARD_INTERVAL_1_4 },
        { "AUTO", GUARD_INTERVAL_AUTO },
        { NULL, 0 }
    };
    return str2enum(guard, guardtab, GUARD_INTERVAL_AUTO);
}

static enum fe_hierarchy str2hier(const char *hier)
{
    struct strtab hiertab[] = {
        { "NONE", HIERARCHY_NONE },
        { "1",    HIERARCHY_1 },
        { "2",    HIERARCHY_2 },
        { "4",    HIERARCHY_4 },
        { "AUTO", HIERARCHY_AUTO },
        { NULL, 0 }
    };
    return str2enum(hier, hiertab, HIERARCHY_AUTO);
}
#endif
/* end shameless copy */

static void ScanNetwork(char *initialdata)
{
    Multiplex_t *mux;
    bool parsed = FALSE;
    __u32 freq;
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
    fe_type_t feType;
    struct dvb_frontend_parameters feparams;
    DVBDiSEqCSettings_t diseqcSettings;
    DVBAdapter_t *adapter = MainDVBAdapterGet();
    int channelCount = 0;
    ListIterator_t iterator;
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
#if defined(ENABLE_DVB)
        case 'T':
            if (sscanf(initialdata, "T %u %4s %4s %4s %6s %4s %4s %4s",
                    &freq, bwStr, fecHiStr, fecLoStr, modStr, transModeStr, guardIntStr, hierarchyStr) == 8)
            {
                feparams.frequency = freq;
                feparams.u.ofdm.bandwidth = str2bandwidth((const char *) bwStr);
                feparams.u.ofdm.code_rate_HP = str2fec((const char *) fecHiStr);
                feparams.u.ofdm.code_rate_LP = str2fec((const char *) fecLoStr);
                feparams.u.ofdm.constellation = str2qam((const char *) modStr);
                feparams.u.ofdm.transmission_mode = str2mode((const char *)transModeStr);
                feparams.u.ofdm.guard_interval = str2guard((const char *) guardIntStr);
                feparams.u.ofdm.hierarchy_information = str2hier((const char *)hierarchyStr);
                feType = FE_OFDM;
                muxFindRange = 166670;
                parsed = TRUE;
            }
            break;

        case 'C':
            if (sscanf(initialdata, "C %u %u %4s %6s", &freq, &symbolRate, fecHiStr, modStr) == 4)
            {
                feparams.u.qam.symbol_rate = symbolRate;
                feparams.u.qam.fec_inner = str2fec(fecHiStr);
                feparams.u.qam.modulation = str2qam(modStr);
                feType = FE_QAM;
                muxFindRange = 0;
                parsed = TRUE;
            }
            break;

        case 'S':
            if (sscanf(initialdata, "S %u %1[HVLR] %u %4s\n", &freq, polarisationStr, &symbolRate, fecHiStr) == 4)
            {
                switch(polarisationStr[0])
                {
                    case 'H':
                    case 'L':
                        diseqcSettings.polarisation = POL_HORIZONTAL;
                        break;
                    default:
                        diseqcSettings.polarisation = POL_VERTICAL;;
                        break;
                }
                diseqcSettings.satellite_number = DVBSSatNumber;
                feparams.frequency = freq;
                feparams.u.qpsk.symbol_rate = symbolRate;
                feparams.u.qpsk.fec_inner = str2fec(fecHiStr);
                feType = FE_QPSK;
                muxFindRange = 0;
                parsed = TRUE;
            }
            break;
#endif

#if defined(ENABLE_ATSC)
        case 'A':
            if (sscanf(initialdata, "A %u %7s\n", &freq, modStr) == 2)
            {
                feparams.frequency = freq;
                feparams.u.vsb.modulation = str2qam(modStr);
                feType = FE_ATSC;
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
        if (adapter->info.type == feType)
        {
            if (adapter->info.caps & FE_CAN_INVERSION_AUTO)
            {
                feparams.inversion = INVERSION_AUTO;
            }
            else
            {
                LogModule(LOG_INFO, SCANNING, "INVERSION_AUTO not supported, trying INVERSION_OFF.\n");
                feparams.inversion = INVERSION_OFF;
            }

            transponderList = ListCreate();
            mux = MultiplexFindFrequencyRange(feparams.frequency, muxFindRange);
            CommandPrintf("Scanning 1 frequencies\n");
            CommandPrintf("%d %u\n", channelCount + 1, feparams.frequency);
            if (TuneFrequency(feType, &feparams, &diseqcSettings, mux, TRUE) == 0)
            {
                 if (ListCount(transponderList) > 0)

                {
                    CommandPrintf("Scanning %d frequencies\n", ListCount(transponderList));
                    for (ListIterator_Init(iterator, transponderList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
                    {
                        TransponderEntry_t *entry = (TransponderEntry_t*)ListIterator_Current(iterator);
                        mux = MultiplexFindId(entry->netId, entry->tsId);
                        feparams = entry->feparams;
                        CommandPrintf("%d %u\n", channelCount + 1, feparams.frequency);
                        if (mux)
                        {
                            CommandPrintf(" Skipped - already found %04x:%04x\n", entry->netId, entry->tsId);
                            MultiplexRefDec(mux);
                        }
                        else
                        {
                            if (feType == QPSK)
                            {
                                mux = MultiplexFindDVBSMultiplex(feparams.frequency, &entry->diseqc);
                            }
                            else
                            {
                                mux = MultiplexFindFrequencyRange(feparams.frequency, muxFindRange);
                            }
                            TuneFrequency(feType, &feparams, &diseqcSettings, mux, TRUE);
                        }
                        channelCount ++;
                    }
                }
            }
            ListFree(transponderList, free);
        }
        else
        {
            CommandError(COMMAND_ERROR_GENERIC, "Type of initial tuning data supplied does not match frontend type!");
        }
    }
    else
    {
        CommandError(COMMAND_ERROR_GENERIC, "Failed to parse initial tuning data!");
    }
}

static int TuneFrequency(fe_type_t type, struct dvb_frontend_parameters *feparams, DVBDiSEqCSettings_t * diseqc, Multiplex_t *mux, bool needNIT)
{
    struct timespec timeout;
    DVBAdapter_t *adapter = MainDVBAdapterGet();
    int result;
    int muxUID;
    bool lockFailed = TRUE;
    TSFilter_t *tsFilter = MainTSFilterGet();


    if (mux != NULL)
    {
        MultiplexRefInc(mux);
    }

    /* Disable TS Packet processing while we tune */
    TSFilterEnable(tsFilter, FALSE);
    FELocked = FALSE;
    waitingForFELocked = TRUE;
    LogModule(LOG_DEBUG, SCANNING, "Trying frequency %d\n", feparams->frequency);
    result = DVBFrontEndTune(adapter, feparams, diseqc);
    if (result == 0)
    {
        clock_gettime( CLOCK_REALTIME, &timeout);
        timeout.tv_sec += 5;

        /* Wait for lock */
        pthread_mutex_lock(&scanningmutex);
        result = pthread_cond_timedwait(&scanningcond, &scanningmutex, &timeout);
        pthread_mutex_unlock(&scanningmutex);
        clock_gettime( CLOCK_REALTIME, &timeout);

        waitingForFELocked = FALSE;
        LogModule(LOG_DEBUG, SCANNING, "FE Locked?%s\n", FELocked ? "Yes":"No");
        if (FELocked && !cancelScan)
        {
            CommandPrintf(" FE Locked\n");
            lockFailed = FALSE;
            if (mux == NULL)
            {
                /* Add multiplex to DBase, set the new multiplex as current and reenabled TSFilter */
                result = MultiplexAdd(type, feparams, diseqc, &muxUID);
                if (result == 0)
                {
                    mux = MultiplexFindUID(muxUID);
                }
            }

            if (mux != NULL)
            {
                ScanMultiplex(mux, needNIT);
                result = 0;
            }
        }
    }

    if (lockFailed && removeFailedFreqs && (mux != NULL) && !cancelScan)
    {
        MultiplexDelete(mux);
    }

    if (mux != NULL)
    {
        MultiplexRefDec(mux);
    }

    return result;
}

static void ScanMultiplex(Multiplex_t *multiplex, bool needNIT)
{
    struct timespec timeout;
    bool seenPATReceived = FALSE;
    bool seenAllPMTReceived = FALSE;
    bool seenSDTReceived = FALSE;
    bool seenNITReceived = FALSE;
    int ret = 0;

    PATReceived = FALSE;
    SDTReceived = FALSE;
    AllPMTReceived = FALSE;
#if defined(ENABLE_DVB)
    NITneeded = needNIT;
#else
    (void)needNIT; /* To prevent compiler warning */
#endif
    PMTCount = 0;
    PMTsReceived = NULL;
    if (!needNIT || !MainIsDVB())
    {
        seenNITReceived = TRUE; /* So we don't wait for a NIT when we don't want one */
    }
    TuningCurrentMultiplexSet(multiplex);

    clock_gettime( CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 15;
    scanning = TRUE;

    while ( !(seenPATReceived && seenAllPMTReceived && seenSDTReceived) && (ret != ETIMEDOUT) && !cancelScan)
    {
        pthread_mutex_lock(&scanningmutex);
        ret = pthread_cond_timedwait(&scanningcond, &scanningmutex, &timeout);
        pthread_mutex_unlock(&scanningmutex);
        if (!seenPATReceived && PATReceived)
        {
            CommandPrintf(" PAT received? Yes\n");
            seenPATReceived = TRUE;
        }

        if (!seenAllPMTReceived && AllPMTReceived)
        {
            CommandPrintf(" PMT received? Yes\n");
            seenAllPMTReceived = TRUE;
        }

        if (!seenSDTReceived && SDTReceived)
        {
            CommandPrintf(" %s received? Yes\n", MainIsDVB() ?"SDT":"VCT");
            seenSDTReceived = TRUE;
        }
    }
    if (!seenPATReceived)
    {
        CommandPrintf(" PAT received? No\n");
    }

    if (!seenAllPMTReceived)
    {
        CommandPrintf(" PMT received? No\n");
    }

    if (!seenSDTReceived)
    {
        CommandPrintf(" %s received? No\n", MainIsDVB() ?"SDT":"VCT");
    }

    if (needNIT)
    {
        struct timespec now;
        clock_gettime( CLOCK_REALTIME, &now);
        timeout.tv_sec = timeout.tv_sec - now.tv_sec;
        timeout.tv_nsec = 0;

        /* There is no way to know that we have received all the NITs so wait until timeout has been reached. */
        nanosleep(&timeout, NULL);
    }

    scanning = FALSE;

    if (PMTsReceived)
    {
        int i;
        DVBAdapter_t *adapter = MainDVBAdapterGet();
        /* Make sure we remove all the PMT filters */
        for (i = 0; i < PMTCount; i ++)
        {
            DVBDemuxReleaseFilter(adapter,PMTsReceived[i].pid);
        }
        ObjectFree(PMTsReceived);
    }


}
static void PATCallback(dvbpsi_pat_t* newpat)
{
    if (scanning && !PATReceived)
    {
        int i;
        dvbpsi_pat_program_t *patentry = newpat->p_first_program;
        TSFilter_t *tsFilter = MainTSFilterGet();
        PMTCount = 0;
        while(patentry)
        {
            if (patentry->i_number != 0x0000)
            {
                PMTCount ++;
            }
            patentry = patentry->p_next;
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
        PATReceived = TRUE;
        tsFilter->tsStructureChanged = TRUE; /* Force all PMTs to be received again incase we are scanning a mux we have pids for */
        if (tsFilter->adapter->hardwareRestricted)
        {
            for (PMTNextIndex = 0;PMTNextIndex < PMTCount; PMTNextIndex ++)
            {
                if (DVBDemuxAllocateFilter(tsFilter->adapter, PMTsReceived[PMTNextIndex].pid,FALSE) != 0)
                {
                    break;
                }
            }
        }
        pthread_mutex_lock(&scanningmutex);
        pthread_cond_signal(&scanningcond);
        pthread_mutex_unlock(&scanningmutex);
    }
}

static void PMTCallback(dvbpsi_pmt_t* newpmt)
{
    if (scanning && !AllPMTReceived)
    {
        bool all = TRUE;
        int i;
        DVBAdapter_t *adapter = MainDVBAdapterGet();
        for (i = 0; i < PMTCount; i ++)
        {
            if (PMTsReceived[i].id == newpmt->i_program_number)
            {
                PMTsReceived[i].received = TRUE;
                if (adapter->hardwareRestricted)
                {
                    DVBDemuxReleaseFilter(adapter,PMTsReceived[i].pid);

                    if (PMTNextIndex < PMTCount)
                    {
                        DVBDemuxAllocateFilter(adapter, PMTsReceived[PMTNextIndex].pid, FALSE);
                        PMTNextIndex ++;
                    }
                }
            }
        }

        for (i = 0; i < PMTCount; i ++)
        {
            if (!PMTsReceived[i].received)
            {
                all = FALSE;
            }
        }

        AllPMTReceived = all;
        if (all)
        {
            pthread_mutex_lock(&scanningmutex);
            pthread_cond_signal(&scanningcond);
            pthread_mutex_unlock(&scanningmutex);
        }
    }
}
#if defined(ENABLE_DVB)
static void SDTCallback(dvbpsi_sdt_t* newsdt)
{
    if (scanning && !SDTReceived)
    {
        SDTReceived = TRUE;
        pthread_mutex_lock(&scanningmutex);
        pthread_cond_signal(&scanningcond);
        pthread_mutex_unlock(&scanningmutex);
    }
}

static fe_bandwidth_t bandwidthTable[] = {BANDWIDTH_8_MHZ, BANDWIDTH_7_MHZ, BANDWIDTH_6_MHZ,
                                          BANDWIDTH_AUTO, BANDWIDTH_AUTO, BANDWIDTH_AUTO,
                                          BANDWIDTH_AUTO, BANDWIDTH_AUTO};
static fe_code_rate_t ofdmCodeRateTable[] = {FEC_1_2, FEC_2_3, FEC_3_4, FEC_4_5,
                                             FEC_5_6, FEC_7_8, FEC_NONE, FEC_NONE};
static fe_modulation_t ofdmConstellationTable[] = {QPSK, QAM_16, QAM_64, QAM_AUTO};
static fe_hierarchy_t ofdmHierarchyTable[] = {HIERARCHY_NONE, HIERARCHY_1, HIERARCHY_2, HIERARCHY_4,
                                              HIERARCHY_NONE, HIERARCHY_1, HIERARCHY_2, HIERARCHY_4};
static fe_guard_interval_t ofdmGuardIntTable[] = {GUARD_INTERVAL_1_32, GUARD_INTERVAL_1_16,
                                                  GUARD_INTERVAL_1_8, GUARD_INTERVAL_1_4};
static fe_transmit_mode_t  ofdmTransmitModeTable[] = { TRANSMISSION_MODE_2K, TRANSMISSION_MODE_8K,
                                                       TRANSMISSION_MODE_AUTO,TRANSMISSION_MODE_AUTO};
static fe_code_rate_t fecInnerTable[] = {FEC_NONE, FEC_1_2, FEC_2_3, FEC_3_4,
                                         FEC_5_6, FEC_7_8, FEC_8_9, FEC_AUTO,
                                         FEC_4_5, FEC_AUTO,FEC_NONE, FEC_NONE,
                                         FEC_NONE, FEC_NONE,FEC_NONE, FEC_NONE};

static enum Polarisation_e polarisationTable[] = {POL_HORIZONTAL, POL_VERTICAL, POL_HORIZONTAL, POL_VERTICAL};

static void NITCallback(dvbpsi_nit_t* newnit)
{
    DVBAdapter_t *adapter = MainDVBAdapterGet();

    TransponderEntry_t *tpEntry;
    int i;
    dvbpsi_nit_transport_t *transport = NULL;
    struct dvb_frontend_parameters feparams;
    DVBDiSEqCSettings_t diseqc = {POL_HORIZONTAL, 0};
    bool moreFreqs = FALSE;

#define ADD_TRANSPONDER() \
    do {\
        if (!FindTransponder(feparams.frequency)) \
        {\
            tpEntry = malloc(sizeof(TransponderEntry_t));\
            if (tpEntry != NULL)\
            {\
                tpEntry->feparams = feparams;\
                tpEntry->diseqc = diseqc; \
                tpEntry->netId = transport->i_original_network_id;\
                tpEntry->tsId = transport->i_ts_id;\
                ListAdd(transponderList, tpEntry);\
            }\
        }\
    }while(FALSE)

    if (NITneeded && scanning)
    {
        for (transport = newnit->p_first_transport; transport; transport = transport->p_next)
        {
            dvbpsi_descriptor_t *descriptor;
            for (descriptor = transport->p_first_descriptor; descriptor; descriptor = descriptor->p_next)
            {
                switch (adapter->info.type)
                {
                    case FE_OFDM:
                        if (descriptor->i_tag == 0x5a)
                        {
                            dvbpsi_terr_deliv_sys_dr_t *terrDelSysDr = dvbpsi_DecodeTerrDelivSysDr(descriptor);
                            if (terrDelSysDr)
                            {
                                feparams.frequency = terrDelSysDr->i_centre_frequency * 10;
                                feparams.u.ofdm.bandwidth = bandwidthTable[terrDelSysDr->i_bandwidth];
                                feparams.u.ofdm.code_rate_HP = ofdmCodeRateTable[terrDelSysDr->i_code_rate_hp_stream];
                                feparams.u.ofdm.code_rate_LP = ofdmCodeRateTable[terrDelSysDr->i_code_rate_lp_stream];
                                feparams.u.ofdm.constellation = ofdmConstellationTable[terrDelSysDr->i_constellation];
                                feparams.u.ofdm.guard_interval = ofdmGuardIntTable[terrDelSysDr->i_guard_interval];
                                feparams.u.ofdm.hierarchy_information = ofdmHierarchyTable[terrDelSysDr->i_hierarchy_information];
                                feparams.u.ofdm.transmission_mode = ofdmTransmitModeTable[terrDelSysDr->i_transmission_mode];
                                moreFreqs = terrDelSysDr->i_other_frequency_flag ? TRUE:FALSE;
                                ADD_TRANSPONDER();
                            }
                            else
                            {
                                moreFreqs = FALSE;
                            }
                        }
                        else if ((descriptor->i_tag == 0x62) && moreFreqs)
                        {
                            dvbpsi_frequency_list_dr_t *freqListDr = dvbpsi_DecodeFrequencyListDr(descriptor);
                            if (freqListDr->i_coding_type == 3)
                            {
                                for (i = 0; i < freqListDr->i_number_of_frequencies; i ++)
                                {
                                    feparams.frequency = freqListDr->p_center_frequencies[i] * 10;
                                    ADD_TRANSPONDER();
                                }
                            }
                        }
                        break;

                    case FE_QPSK:
                        if (descriptor->i_tag == 0x43)
                        {
                            dvbpsi_sat_deliv_sys_dr_t *satDelSysDr = dvbpsi_DecodeSatDelivSysDr(descriptor);
                            double freq = BCDFixedPoint3_7ToDouble(satDelSysDr->i_frequency);
                            double symbolRate =BCDFixedPoint3_7ToDouble(satDelSysDr->i_symbol_rate << 4);

                            feparams.frequency = (__u32)(freq * 1000000.0);
                            feparams.u.qpsk.fec_inner = fecInnerTable[satDelSysDr->i_fec_inner];
                            feparams.u.qpsk.symbol_rate = (__u32)(symbolRate * 1000000.0);
                            diseqc.polarisation = polarisationTable[satDelSysDr->i_polarization];
                            diseqc.satellite_number = DVBSSatNumber;
                            ADD_TRANSPONDER();
                            moreFreqs = TRUE;
                        }
                        else if ((descriptor->i_tag == 0x62) && moreFreqs)
                        {
                            dvbpsi_frequency_list_dr_t *freqListDr = dvbpsi_DecodeFrequencyListDr(descriptor);
                            if (freqListDr->i_coding_type == 1)
                            {
                                for (i = 0; i < freqListDr->i_number_of_frequencies; i ++)
                                {
                                    double freq = BCDFixedPoint3_7ToDouble(freqListDr->p_center_frequencies[i]);
                                    feparams.frequency =  (__u32)(freq * 1000000.0);
                                    ADD_TRANSPONDER();
                                }
                            }
                        }
                        break;

                    case FE_QAM:
                        if (descriptor->i_tag == 0x44)
                        {
                        }
                        break;
                    default:
                        break;
                }
            }
        }
    }
}

static bool FindTransponder(int freq)
{
    ListIterator_t iterator;
    for (ListIterator_Init(iterator, transponderList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        TransponderEntry_t *entry = (TransponderEntry_t *)ListIterator_Current(iterator);
        if (entry->feparams.frequency == freq)
        {
            return TRUE;
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
static void VCTCallback(dvbpsi_atsc_vct_t* newvct)
{
    if (scanning && !SDTReceived)
    {
        SDTReceived = TRUE;
        pthread_mutex_lock(&scanningmutex);
        pthread_cond_signal(&scanningcond);
        pthread_mutex_unlock(&scanningmutex);
    }
}
#endif

static void FELockedEventListener(void *arg, Event_t event, void *payload)
{
    if (waitingForFELocked)
    {
        FELocked = TRUE;
        pthread_mutex_lock(&scanningmutex);
        pthread_cond_signal(&scanningcond);
        pthread_mutex_unlock(&scanningmutex);
    }
}

