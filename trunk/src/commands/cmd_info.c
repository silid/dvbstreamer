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

info.c

Command functions to supply the user with information about the system.

*/
#define _GNU_SOURCE
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
#include "outputs.h"
#include "main.h"
#include "deliverymethod.h"
#include "plugin.h"
#include "servicefilter.h"
#include "tuning.h"

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void CommandListServices(int argc, char **argv);
static void CommandListMuxes(int argc, char **argv);
static void CommandCurrent(int argc, char **argv);
static void CommandServiceInfo(int argc, char **argv);
static void CommandMuxInfo(int argc, char **argv);
static void CommandStats(int argc, char **argv);
static void CommandFEStatus(int argc, char **argv);

static void CommandVariableUptimeGet(char *name);
static void CommandVariableFETypeGet(char *name);
/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
Command_t CommandDetailsInfo[] =
{
    {
        "lsservices",
        TRUE, 0, 1,
        "List all services or for a specific multiplex.",
        "lsservies [mux | <multiplex frequency>]\n"
        "Lists all the services currently in the database if no multiplex is specified or"
        "if \"mux\" is specified only the services available of the current mux or if a"
        " frequency is specified only the services available on that multiplex.",
        CommandListServices
    },
    {
        "lsmuxes",
        FALSE, 0, 0,
        "List multiplexes.",
        "List all multiplexes.",
        CommandListMuxes
    },
    {
        "current",
        FALSE, 0, 0,
        "Print out the service currently being streamed.",
        "Shows the service that is currently being streamed to the default output.",
        CommandCurrent
    },
    {
        "serviceinfo",
        FALSE, 1, 1,
        "Display information about a service.",
        "serviceinfo <service name>\n"
        "Displays information about the specified service.",
        CommandServiceInfo
    },
    {
        "muxinfo",
        TRUE, 1, 2,
        "Display information about a mux.",
        "muxinfo <frequency> or\n"
        "muxinfo <net id> <ts id>\n"
        "Displays information about the specified service.",
        CommandMuxInfo
    },
    {
        "stats",
        FALSE, 0, 0,
        "Display the stats for the PAT,PMT and service PID filters.",
        "Display the number of packets processed for the PSI/SI filters and the number of"
        " packets filtered for each service filter and manual output.",
        CommandStats
    },
    {
        "festatus",
        FALSE, 0, 0,
        "Displays the status of the tuner.",
        "Displays whether the front end is locked, the bit error rate and signal to noise"
        "ratio and the signal strength",
        CommandFEStatus
    },
    {NULL, FALSE, 0, 0, NULL,NULL}
};

static char *FETypesStr[] = {
    "QPSK",
    "QAM",
    "OFDM",
    "ATSC"
};

static CommandVariable_t VariableUptime = {"uptime", CommandVariableUptimeGet, NULL};
static CommandVariable_t VariableUpsecs = {"upsecs", CommandVariableUptimeGet, NULL};
static CommandVariable_t VariableFetype = {"fetype", CommandVariableFETypeGet, NULL};

static time_t StartTime;
/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
void CommandInstallInfo(void)
{
    CommandRegisterCommands(CommandDetailsInfo);
    
    CommandRegisterVariable(&VariableUptime);
    CommandRegisterVariable(&VariableUpsecs);    
    CommandRegisterVariable(&VariableFetype);
    StartTime = time(NULL);
}

void CommandUnInstallInfo(void)
{
    CommandUnRegisterCommands(CommandDetailsInfo); 
    CommandUnRegisterVariable(&VariableUptime);
    CommandUnRegisterVariable(&VariableUpsecs);    
    CommandUnRegisterVariable(&VariableFetype);
}

/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/
static void CommandListServices(int argc, char **argv)
{
    ServiceEnumerator_t enumerator = NULL;
    Service_t *service;

    /* Make sure the database is up-to-date before displaying the names */
    UpdateDatabase();

    if (argc == 1)
    {
        char *mux = argv[0];
        Multiplex_t *multiplex = NULL;
        if (strcmp(mux, "mux") == 0)
        {
            multiplex = TuningCurrentMultiplexGet();
            if (!multiplex)
            {
                CommandPrintf("No multiplex currently selected!\n");
                return;
            }
        }
        else
        {
            int freq = atoi(mux);
            multiplex = MultiplexFindFrequency(freq);
        }
        if (multiplex)
        {
            enumerator = ServiceEnumeratorForMultiplex(multiplex);
        }
        if (enumerator == NULL)
        {
            CommandPrintf("Failed to find multiplex \"%s\"\n", mux);
            return;
        }
    }
    else
    {
        enumerator = ServiceEnumeratorGet();
    }

    if (enumerator != NULL)
    {
        do
        {
            service = ServiceGetNext(enumerator);
            if (service)
            {
                CommandPrintf("%s\n", service->name);
                ServiceRefDec(service);
            }
        }
        while(service && !ExitProgram);
        ServiceEnumeratorDestroy(enumerator);
    }
}

static void CommandListMuxes(int argc, char **argv)
{
    MultiplexEnumerator_t enumerator = MultiplexEnumeratorGet();
    Multiplex_t *multiplex;
    do
    {
        multiplex = MultiplexGetNext(enumerator);
        if (multiplex)
        {
            CommandPrintf("%d\n", multiplex->freq);
            MultiplexRefDec(multiplex);
        }
    }while(multiplex && ! ExitProgram);
    MultiplexEnumeratorDestroy(enumerator);
}

static void CommandCurrent(int argc, char **argv)
{
    Service_t *service = TuningCurrentServiceGet();
    if ( service)
    {
        Multiplex_t *multiplex = TuningCurrentMultiplexGet();
        
        CommandPrintf("Current Service : \"%s\" (0x%04x) Multiplex: %d\n",
            service->name, service->id, multiplex->freq);
        ServiceRefDec(service);
        MultiplexRefDec(multiplex);
    }
    else
    {
        CommandPrintf("No current service\n");
    }
}
static void CommandServiceInfo(int argc, char **argv)
{
    Service_t *service;
    Multiplex_t *multiplex;

    service = CacheServiceFindName(argv[0], &multiplex);
    if (service)
    {
        static const char *serviceType[]= {"Digital TV", "Digital Radio", "Data", "Unknown"};
            
        CommandPrintf("Service ID          : 0x%04x\n", service->id);
        CommandPrintf("Network ID          : 0x%04x\n", multiplex->networkId);
        CommandPrintf("Transport Stream ID : 0x%04x\n", multiplex->tsId);
        CommandPrintf("Source              : 0x%04x\n", service->source);
        CommandPrintf("Conditional Access? : %s\n", service->conditionalAccess ? "CA":"Free to Air");
        CommandPrintf("Type                : %s\n", serviceType[service->type]);
        CommandPrintf("PMT PID             : 0x%04x\n", service->pmtPid);
        CommandPrintf("    Version         : %d\n", service->pmtVersion);
        ServiceRefDec(service);
        MultiplexRefDec(multiplex);
    }
    else
    {
        CommandError(COMMAND_ERROR_GENERIC, "Service not found!");
    }
}

static void CommandMuxInfo(int argc, char **argv)
{
    Multiplex_t *multiplex = NULL;
    if (argc == 1)
    {
        int freq = atoi(argv[0]);
        multiplex = MultiplexFindFrequency(freq);
    }
    if (argc == 2)
    {
        int netId = 0;
        int tsId = 0;
        sscanf(argv[0], "%x", &netId);
        sscanf(argv[1], "%x", &tsId);        
        multiplex = MultiplexFindId(netId, tsId);
    }
    if (multiplex)
    {
        CommandPrintf("Network ID          : 0x%04x\n", multiplex->networkId);
        CommandPrintf("Transport Stream ID : 0x%04x\n", multiplex->tsId);
        CommandPrintf("PAT Version         : %d\n", multiplex->patVersion);
        CommandPrintf("Internal UID        : 0x%08x\n", multiplex->uid);
        CommandPrintf("Tuning Parameters\n");
        CommandPrintf("    Frequency       : %d\n", multiplex->freq);
        CommandPrintf("    Type            : %s\n", FETypesStr[multiplex->type]);
        
        MultiplexRefDec(multiplex);
    }
    else
    {
        CommandError(COMMAND_ERROR_GENERIC, "Multiplex not found!");
    }
}


static void CommandStats(int argc, char **argv)
{
    ListIterator_t iterator;
    TSFilter_t *tsFilter = MainTSFilterGet();
    
    CommandPrintf("PSI/SI Processor Statistics\n"
                  "---------------------------\n");

    for ( ListIterator_Init(iterator, tsFilter->pidFilters); 
          ListIterator_MoreEntries(iterator); 
          ListIterator_Next(iterator))
    {
        PIDFilter_t *filter = (PIDFilter_t *)ListIterator_Current(iterator);
        if (filter->outputPacket == NULL)
        {
            CommandPrintf("\t%-15s : %d\n", filter->name, filter->packetsProcessed);
        }
    }
    CommandPrintf("\n");

    CommandPrintf("Service Filter Statistics\n"
                  "-------------------------\n");
    for ( ListIterator_Init(iterator, ServiceOutputsList); 
          ListIterator_MoreEntries(iterator); 
          ListIterator_Next(iterator))
    {
        Output_t *output = ListIterator_Current(iterator);
        CommandPrintf("\t%-15s : %d\n", output->name, output->filter->packetsOutput);
    }
    CommandPrintf("\n");

    CommandPrintf("Manual Output Statistics\n"
                  "------------------------\n");
     for ( ListIterator_Init(iterator, ManualOutputsList); 
           ListIterator_MoreEntries(iterator); 
           ListIterator_Next(iterator))
    {
        Output_t *output = ListIterator_Current(iterator);
        CommandPrintf("\t%-15s : %d\n", output->name, output->filter->packetsOutput);
    }
    CommandPrintf("\n");



    CommandPrintf("Total packets processed: %d\n", tsFilter->totalPackets);
    CommandPrintf("Approximate TS bitrate : %gMbs\n", ((double)tsFilter->bitrate / (1024.0 * 1024.0)));
}


static void CommandFEStatus(int argc, char **argv)
{
    fe_status_t status;
    unsigned int ber, strength, snr;
    DVBFrontEndStatus(MainDVBAdapterGet(), &status, &ber, &strength, &snr);

    CommandPrintf("Tuner status:  %s%s%s%s%s%s\n",
             (status & FE_HAS_SIGNAL)?"Signal, ":"",
             (status & FE_TIMEDOUT)?"Timed out, ":"",
             (status & FE_HAS_LOCK)?"Lock, ":"",
             (status & FE_HAS_CARRIER)?"Carrier, ":"",
             (status & FE_HAS_VITERBI)?"VITERBI, ":"",
             (status & FE_HAS_SYNC)?"Sync, ":"");
    CommandPrintf("BER = %d Signal Strength = %d SNR = %d\n", ber, strength, snr);
}

/*******************************************************************************
* Variable Functions                                                           *
*******************************************************************************/
static void CommandVariableUptimeGet(char *name)
{
    if (strcmp(name, "uptime") == 0)
    {
        time_t now;
        int seconds;
        int d, h, m, s;
        time(&now);
        seconds = (int)difftime(now, StartTime);
        d = seconds / (24 * 60 * 60);
        h = (seconds - (d * 24 * 60 * 60)) / (60 * 60);
        m = (seconds - ((d * 24 * 60 * 60) + (h * 60 * 60))) / 60;
        s = (seconds - ((d * 24 * 60 * 60) + (h * 60 * 60) + (m * 60)));
        CommandPrintf("%d Days %d Hours %d Minutes %d seconds\n", d, h, m, s);
    }else if (strcmp(name, "upsecs") == 0)
    {
        time_t now;
        time(&now);

        CommandPrintf("%d\n", (int)difftime(now, StartTime));
    }
}

static void CommandVariableFETypeGet(char *name)
{

    DVBAdapter_t *adapter = MainDVBAdapterGet();

    CommandPrintf("%s\n", FETypesStr[adapter->info.type]);
}
