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
#include "psipprocessor.h"

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
static void ScanMultiplex(Multiplex_t *multiplex);
static void PATCallback(dvbpsi_pat_t* newpat);
static void PMTCallback(dvbpsi_pmt_t* newpmt);
static void SDTCallback(dvbpsi_sdt_t* newsdt);
static void VCTCallback(dvbpsi_atsc_vct_t* newvct);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
Command_t CommandDetailsScanning[] = 
{
    {
        "scan",
        TRUE, 1,1,
        "Scan the specified multiplex for services.",
        "scan <multiplex>\n"
        "Tunes to the specified multiplex and wait 5 seconds for PAT/PMT/SDT."
        " If multiplex is 'all' then all multiplexes will be scanned.",
        CommandScan
    },
    {NULL, FALSE, 0, 0, NULL,NULL}
};


static bool scanning = FALSE;
static bool PATReceived = FALSE;
static bool AllPMTReceived = FALSE;
static bool SDTReceived = FALSE;
static int PMTCount = 0;
static struct PMTReceived_t *PMTsReceived = NULL;
static pthread_mutex_t scanningmutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t scanningcond = PTHREAD_COND_INITIALIZER;
/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
void CommandInstallScanning(void)
{
    if (MainIsDVB())
    {
        SDTProcessorRegisterSDTCallback(SDTCallback);
    }
    else
    {
        PSIPProcessorRegisterVCTCallback(VCTCallback);
    }
    PATProcessorRegisterPATCallback(PATCallback);
    PMTProcessorRegisterPMTCallback(PMTCallback);    
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

    CommandCheckAuthenticated();
    currentService = TuningCurrentServiceGet();

    if (strcmp(argv[0], "all") == 0)
    {
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
                ScanMultiplex(multiplexes[i]);
                MultiplexRefDec(multiplexes[i]);
            }

            ObjectFree(multiplexes);
        }
    }
    else
    {
        multiplex = MultiplexFind(argv[0]);
        if (multiplex)
        {
            ScanMultiplex(multiplex);
            MultiplexRefDec(multiplex);
        }
    }

    if (currentService)
    {
        TuningCurrentServiceSet(currentService);
        ServiceRefDec(currentService);
    }
}
/************************** Scan Callback Functions **************************/

static void ScanMultiplex(Multiplex_t *multiplex)
{
    struct timespec timeout;
    bool seenPATReceived = FALSE;
    bool seenAllPMTReceived = FALSE;
    bool seenSDTRecieved = FALSE;
    int ret = 0;
    
    CommandPrintf("Scanning %d\n", multiplex->uid);

    TuningCurrentMultiplexSet(multiplex);

    PATReceived = FALSE;
    SDTReceived = FALSE;
    AllPMTReceived = FALSE;
    PMTCount = 0;
    PMTsReceived = NULL;
    clock_gettime( CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 5;
    scanning = TRUE;

    while ( !(seenPATReceived && seenAllPMTReceived && seenSDTRecieved) && (ret != ETIMEDOUT))
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

        if (!seenSDTRecieved && SDTReceived)
        {
            CommandPrintf(" %s received? Yes\n", MainIsDVB() ?"SDT":"VCT");
            seenSDTRecieved = TRUE;
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

    if (!seenSDTRecieved)
    {
        CommandPrintf(" %s received? No\n", MainIsDVB() ?"SDT":"VCT");
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
        if (SDTReceived)
        {
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
        if (SDTReceived)
        {
            pthread_mutex_lock(&scanningmutex);
            pthread_cond_signal(&scanningcond);
            pthread_mutex_unlock(&scanningmutex);
        }
    }
}

