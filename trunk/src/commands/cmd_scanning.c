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
#include "dvb.h"
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

/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/
struct PMTReceived_t
{
    uint16_t id;
    uint16_t pid;
    bool received;
};

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void CommandScan(int argc, char **argv);
static void CommandScanCancel(int argc, char **argv);
static void ScanCurrentMultiplexes(void);
static void ScanFullDVBT(bool removeFailed);
static void ScanFullDVBC(bool removeFailed);
static void ScanFullATSC(bool removeFailed, bool scanOTA, bool scanCable);
static void ScanNetwork(char *initialdata);
static void ScanMultiplex(Multiplex_t *multiplex, bool needNIT);
static int TuneFrequency(fe_type_t type, struct dvb_frontend_parameters *feparams, DVBDiSEqCSettings_t * diseqc, Multiplex_t *mux, bool removeFailed);
static void PATCallback(dvbpsi_pat_t* newpat);
static void PMTCallback(dvbpsi_pmt_t* newpmt);
static void SDTCallback(dvbpsi_sdt_t* newsdt);
static void NITCallback(dvbpsi_nit_t* newnit);
static void VCTCallback(dvbpsi_atsc_vct_t* newvct);
static void FELockedEventListener(void *arg, Event_t event, void *payload);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
Command_t CommandDetailsScanning[] = 
{
    {
        "scan",
        TRUE, 1,1,
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
static bool scanning = FALSE;
static bool cancelScan = FALSE;
static bool PATReceived = FALSE;
static bool AllPMTReceived = FALSE;
static bool SDTReceived = FALSE;
static bool NITReceived = FALSE;
static bool NITneeded = FALSE;
static bool waitingForFELocked= FALSE;
static bool FELocked = FALSE;
static int PMTCount = 0;
static struct PMTReceived_t *PMTsReceived = NULL;
static pthread_mutex_t scanningmutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t scanningcond = PTHREAD_COND_INITIALIZER;
/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
void CommandInstallScanning(void)
{
    Event_t feLockedEvent;
    LogModule(LOG_DEBUG,SCANNING,"Starting to install scanning.\n");
    if (MainIsDVB())
    {
        SDTProcessorRegisterSDTCallback(SDTCallback);
        NITProcessorRegisterNITCallback(NITCallback);
    }
    else
    {
        PSIPProcessorRegisterVCTCallback(VCTCallback);
    }
    PATProcessorRegisterPATCallback(PATCallback);
    PMTProcessorRegisterPMTCallback(PMTCallback);
    LogModule(LOG_DEBUG,SCANNING,"Finding fe locked event.\n");
    feLockedEvent = EventsFindEvent("DVBAdapter.Locked");
    EventsRegisterEventListener(feLockedEvent, FELockedEventListener, NULL);
    LogModule(LOG_DEBUG,SCANNING,"Installing commands.\n");
    CommandRegisterCommands(CommandDetailsScanning);
}

void CommandUnInstallScanning(void)
{
    CommandUnRegisterCommands(CommandDetailsScanning);
    scanning = FALSE;
    PATProcessorUnRegisterPATCallback(PATCallback);
    PMTProcessorUnRegisterPMTCallback(PMTCallback);
    if (MainIsDVB())
    {
        SDTProcessorUnRegisterSDTCallback(SDTCallback);
        NITProcessorUnRegisterNITCallback(NITCallback);
    }
    else
    {
        PSIPProcessorUnRegisterVCTCallback(VCTCallback);
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
    bool removeFailed = TRUE;
    bool atscScanOTA = TRUE;
    bool atscScanCable = TRUE;
    
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
            case FE_OFDM:
                ScanFullDVBT(removeFailed);
                break;
            case FE_QAM:
                ScanFullDVBC(removeFailed);
                break;
            case FE_ATSC:
                ScanFullATSC(removeFailed, atscScanOTA, atscScanCable);
                break;
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

static void ScanFullDVBT(bool removeFailed)
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
    Multiplex_t *mux;

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
        feparams.frequency = 142500000 + (channel * 7000000);
        CommandPrintf("%u\n", feparams.frequency);
        mux = MultiplexFindFrequencyRange(feparams.frequency, 166670);
        TuneFrequency(FE_OFDM, &feparams, NULL, mux, removeFailed);
    }

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
        feparams.frequency = 306000000 + (channel * 8000000);
        CommandPrintf("%u\n", feparams.frequency);
        mux = MultiplexFindFrequencyRange(feparams.frequency, 166670);
        TuneFrequency(FE_OFDM, &feparams, NULL, mux, removeFailed);
    }
    
}

static void ScanFullDVBC(bool removeFailed)
{
}

static void ScanFullATSC(bool removeFailed, bool scanOTA, bool scanCable)
{
    Multiplex_t *mux;
    int channel;
    int base_offset = 0;
    struct dvb_frontend_parameters feparams;

    if (scanOTA)
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
            CommandPrintf("%u\n", feparams.frequency);
            mux = MultiplexFindFrequencyRange(feparams.frequency, 28615);
            TuneFrequency(FE_ATSC, &feparams, NULL, mux, removeFailed);
        }
    }
    if (scanCable)
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
            CommandPrintf("%u\n", feparams.frequency);
            mux = MultiplexFindFrequency(feparams.frequency);
            TuneFrequency(FE_ATSC, &feparams, NULL, mux, removeFailed);
        }
    }
}

static void ScanNetwork(char *initialdata)
{
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
     
    
}

static int TuneFrequency(fe_type_t type, struct dvb_frontend_parameters *feparams, DVBDiSEqCSettings_t * diseqc, Multiplex_t *mux, bool removeFailed)
{
    struct timespec timeout;
    DVBAdapter_t *adapter = MainDVBAdapterGet();
    int result;
    int muxUID;
    bool tuneFailed = TRUE;
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
    result = DVBFrontEndTune(adapter, feparams, NULL);
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
            tuneFailed = FALSE;
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
                ScanMultiplex(mux, FALSE);
                result = 0;
            }
        }
    }

    if (tuneFailed && removeFailed && (mux != NULL) && !cancelScan)
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

    TuningCurrentMultiplexSet(multiplex);

    PATReceived = FALSE;
    SDTReceived = FALSE;
    AllPMTReceived = FALSE;
    NITneeded = needNIT;
    NITReceived = FALSE;
    PMTCount = 0;
    PMTsReceived = NULL;
    if (!needNIT)
    {
        seenNITReceived = TRUE; /* So we don't wait for a NIT when we don't want one */
    }
    TuningCurrentMultiplexSet(multiplex);

    clock_gettime( CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 10;
    scanning = TRUE;
    
    while ( !(seenPATReceived && seenAllPMTReceived && seenSDTReceived && seenNITReceived) && (ret != ETIMEDOUT) && !cancelScan)
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
        
        if (needNIT && !seenNITReceived && NITReceived)
        {
            CommandPrintf(" NIT received? Yes\n");
            seenNITReceived = TRUE;
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
    
    if (!seenNITReceived && needNIT)
    {
        CommandPrintf(" NIT received? No\n");
    }
    
    scanning = FALSE;

    if (PMTsReceived)
    {
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
            DVBDemuxAllocateFilter(tsFilter->adapter, PMTsReceived[0].pid,FALSE);
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
                    if (i + 1 < PMTCount)
                    {
                        DVBDemuxAllocateFilter(adapter, PMTsReceived[i + 1].pid,FALSE);
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

static void NITCallback(dvbpsi_nit_t* newnit)
{
    if (NITneeded)
    {
        /* Process the NIT and store the other networks for future scanning. */
        if(scanning && !NITReceived)
        {
            NITReceived = TRUE;
            pthread_mutex_lock(&scanningmutex);
            pthread_cond_signal(&scanningcond);
            pthread_mutex_unlock(&scanningmutex);
        }
    }
}

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
