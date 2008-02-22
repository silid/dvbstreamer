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

/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/
#define FILTER_TYPE_NOT_USED 0
#define FILTER_TYPE_TV       1
#define FILTER_TYPE_RADIO    2
#define FILTER_TYPE_DATA     4
#define FILTER_TYPE_UNKNOWN  8

#define FILTER_ACCESS_NOT_USED 0
#define FILTER_ACCESS_FTA      1
#define FILTER_ACCESS_CA       2

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void CommandListServices(int argc, char **argv);
static bool FilterService(Service_t *service, uint32_t filterByType, uint32_t filterByAccess, char* provider);
static void CommandListMuxes(int argc, char **argv);
static void CommandListPids(int argc, char **argv);
static char *GetStreamTypeString(int type);
static void CommandCurrent(int argc, char **argv);
static void CommandServiceInfo(int argc, char **argv);
static void CommandMuxInfo(int argc, char **argv);
static void CommandStats(int argc, char **argv);
static void CommandFEStatus(int argc, char **argv);

static void CommandVariableUptimeGet(char *name);
static void CommandVariableFETypeGet(char *name);
static void CommandVariableFENameGet(char *name);
static void CommandVariableTSModeGet(char *name);
/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
Command_t CommandDetailsInfo[] =
{
    {
        "lsservices",
        TRUE, 0, 6,
        "List all services or for a specific multiplex.",
        "lsservices [-id] [filters] [multiplex]\n"
        "Lists selected services, by default all services on all multiplex are displayed.\n"
        "\n"
        "-id\n"
        "List the services fully quailified id.\n"
        "\n"
        "filters (tv, radio, data, unknown)\n"
        "Multiple filters can be specified or if no filters are specified all selected"
        " services will be displayed\n"
        "\n"
        "multiplex (\'mux\'| uid | netid.tsid | frequency)\n"
        "Select only services on the specified multiplex, where \'mux\' indiciated the current multiplex.",
        CommandListServices
    },
    {
        "lsmuxes",
        TRUE, 0, 1,
        "List multiplexes.",
        "lsmuxes [-id]\n"
        "List all available multiplex UIDs.\n"
        "\n"
        "-id\n"
        "List the multiplexes network id.ts id",
        CommandListMuxes
    },
    {
        "lspids",
        TRUE, 1, 2,
        "List the PIDs for a specified service.",
        "lspids <service name>\n"
        "List all the PIDs specified in <service name> PMT.",
        CommandListPids
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
        "muxinfo <uid> or\n"
        "muxinfo <netid>.<tsid>\n"
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

static CommandVariable_t VariableUptime = {
    "uptime", 
    "Number of days/hours/minutes/seconds this instance has been running.", 
    CommandVariableUptimeGet, 
    NULL
    };

static CommandVariable_t VariableUpsecs = {
    "upsecs", 
    "Number of seconds this instance has been running.",
    CommandVariableUptimeGet, 
    NULL
    };

static CommandVariable_t VariableFEType = {
    "fetype", 
    "Type of the tuner this instance is using.",
    CommandVariableFETypeGet, 
    NULL
    };

static CommandVariable_t VariableFEName = {
    "fename", 
    "Name of the tuner this instance is using.",
    CommandVariableFENameGet, 
    NULL
    };

static CommandVariable_t VariableTSMode = {
    "tsmode", 
    "Whether this instance is running in Full TS or hardware restricted mode.",
    CommandVariableTSModeGet, 
    NULL
    };

static time_t StartTime;
/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
void CommandInstallInfo(void)
{
    CommandRegisterCommands(CommandDetailsInfo);
    
    CommandRegisterVariable(&VariableUptime);
    CommandRegisterVariable(&VariableUpsecs);    
    CommandRegisterVariable(&VariableFEType);
    CommandRegisterVariable(&VariableFEName);    
    CommandRegisterVariable(&VariableTSMode);    
    StartTime = time(NULL);
}

void CommandUnInstallInfo(void)
{
    CommandUnRegisterCommands(CommandDetailsInfo); 
    CommandUnRegisterVariable(&VariableUptime);
    CommandUnRegisterVariable(&VariableUpsecs);    
    CommandUnRegisterVariable(&VariableFEType);
    CommandUnRegisterVariable(&VariableFEName);    
    CommandUnRegisterVariable(&VariableTSMode);     
}

/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/
static void CommandListServices(int argc, char **argv)
{
    ServiceEnumerator_t enumerator = NULL;
    Service_t *service;
    Multiplex_t *multiplex = NULL;
    int i;
    bool dvbIds = FALSE;
    uint32_t filterByType = FILTER_TYPE_NOT_USED;
    uint32_t filterByAccess = FILTER_ACCESS_NOT_USED;
    char *provider = NULL;
    char *providerStr = "provider=";

    /* Make sure the database is up-to-date before displaying the names */
    UpdateDatabase();

    for (i = 0; i < argc; i ++)
    {
        if (strcmp(argv[i], "-id") == 0)
        {
            dvbIds = TRUE;
        }
        else if (strcmp(argv[i], "mux") == 0)
        {
            if (multiplex)
            {
                MultiplexRefDec(multiplex);
            }
            multiplex = TuningCurrentMultiplexGet();
            if (!multiplex)
            {
                CommandPrintf("No multiplex currently selected!\n");
                return;
            }
        }
        else if (strcmp(argv[i], "tv") == 0)
        {
            filterByType |= FILTER_TYPE_TV;
        }
        else if (strcmp(argv[i], "radio") == 0)
        {
            filterByType |= FILTER_TYPE_RADIO;
        }
        else if (strcmp(argv[i], "data") == 0)
        {
            filterByType |= FILTER_TYPE_DATA;
        }
        else if (strcmp(argv[i], "unknown") == 0)
        {
            filterByType |= FILTER_TYPE_UNKNOWN;
        }
        else if (strcmp(argv[i], "fta") == 0)
        {
            filterByAccess |= FILTER_ACCESS_FTA;
        }
        else if (strcmp(argv[i], "ca") == 0)
        {
            filterByAccess |= FILTER_ACCESS_CA;
        }
        else if (strncmp(argv[i], providerStr, strlen(providerStr)) == 0)
        {
            provider = argv[i] + strlen(providerStr);
        }
        else
        {
            if (multiplex)
            {
                MultiplexRefDec(multiplex);
            }
            multiplex = MultiplexFind(argv[i]);
            if (!multiplex)
            {
                CommandPrintf("Failed to find multiplex \"%s\"\n", argv[i]);
                return;                    
            }
            
        }
    }


    if (multiplex)
    {
        enumerator = ServiceEnumeratorForMultiplex(multiplex);
        MultiplexRefDec(multiplex);
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
                if (FilterService(service, filterByType, filterByAccess, provider))
                {
                    if (dvbIds)
                    {
                        multiplex = MultiplexFindUID(service->multiplexUID);
                        CommandPrintf("%04x.%04x.%04x : %s\n", 
                            multiplex->networkId & 0xffff, multiplex->tsId & 0xffff,
                            service->id, service->name);
                        MultiplexRefDec(multiplex);
                    }
                    else
                    {
                        CommandPrintf("%s\n", service->name);
                    }
                }
                ServiceRefDec(service);
            }
        }
        while(service && !ExitProgram);
        ServiceEnumeratorDestroy(enumerator);
    }
}

static bool FilterService(Service_t *service, uint32_t filterByType, uint32_t filterByAccess, char* provider)
{
    bool filterByTypeResult = FALSE;
    bool filterByAccessResult = FALSE;
    bool filterByProviderResult = FALSE;
    
    if (filterByType)
    {
        if ((filterByType & FILTER_TYPE_TV )&& (service->type == ServiceType_TV))
        {
            filterByTypeResult = TRUE;
        }
        if ((filterByType & FILTER_TYPE_RADIO)&& (service->type == ServiceType_Radio))
        {
            filterByTypeResult = TRUE;
        }
        if ((filterByType & FILTER_TYPE_DATA)&& (service->type == ServiceType_Data))
        {
            filterByTypeResult = TRUE;
        }
        if ((filterByType & FILTER_TYPE_UNKNOWN)&& (service->type == ServiceType_Unknown))
        {
            filterByTypeResult = TRUE;
        }
    }
    else
    {
        filterByTypeResult = TRUE;
    }

    if (filterByAccess)
    {
        if ((filterByAccess & FILTER_ACCESS_FTA) && !service->conditionalAccess)
        {
            filterByAccessResult = TRUE;
        }        
        if ((filterByAccess & FILTER_ACCESS_CA) && service->conditionalAccess)
        {
            filterByAccessResult = TRUE;
        }
    }
    else
    {
        filterByAccessResult = TRUE;
    }

    if (provider)
    {
        filterByProviderResult = service->provider && (strcmp(provider, service->provider) == 0);
    }    
    else
    {
        filterByProviderResult = TRUE;
    }
    return filterByTypeResult && filterByAccessResult && filterByProviderResult;
}

static void CommandListMuxes(int argc, char **argv)
{
    MultiplexEnumerator_t enumerator = MultiplexEnumeratorGet();
    Multiplex_t *multiplex;
    bool ids = FALSE;
    if ((argc == 1) && (strcmp(argv[0], "-id") == 0))
    {
        ids = TRUE;
    }
    do
    {
        multiplex = MultiplexGetNext(enumerator);
        if (multiplex)
        {
            if (ids)
            {
                CommandPrintf("%04x.%04x : %d \n", 
                    multiplex->networkId & 0xffff, multiplex->tsId & 0xffff, multiplex->uid);
            }
            else
            {
                CommandPrintf("%d\n", multiplex->uid);
            }
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
        
        CommandPrintf("%04x.%04x.%04x : \"%s\"\n",
            multiplex->networkId & 0xffff, multiplex->tsId & 0xffff, service->id & 0xffff,
            service->name);
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

    UpdateDatabase();
    
    service = ServiceFindName(argv[0]);

    if (service)
    {
        static const char *serviceType[]= {"Digital TV", "Digital Radio", "Data", "Unknown"};
        multiplex = MultiplexFindUID(service->multiplexUID);            
        CommandPrintf("Name                : %s\n", service->name);
        CommandPrintf("Provider            : %s\n", service->provider);
        CommandPrintf("Type                : %s\n", serviceType[service->type]);
        CommandPrintf("Conditional Access? : %s\n", service->conditionalAccess ? "CA":"Free to Air");
        CommandPrintf("ID                  : %04x.%04x.%04x\n", multiplex->networkId, multiplex->tsId, service->id);
        CommandPrintf("Multiplex UID       : %d\n", service->multiplexUID);
        CommandPrintf("Source              : 0x%04x\n", service->source);
        CommandPrintf("Default Authority   : %s\n", service->defaultAuthority);
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
        multiplex = MultiplexFind(argv[0]);
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
        CommandPrintf("UID                 : %d\n", multiplex->uid);
        CommandPrintf("ID                  : %04x.%04x\n", multiplex->networkId, multiplex->tsId);
        CommandPrintf("PAT Version         : %d\n", multiplex->patVersion);
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
        if (strcmp(filter->type, PSISIPIDFilterType) == 0)
        {
            CommandPrintf("\t%-15s : %lld\n", filter->name, filter->packetsProcessed);
        }
    }
    CommandPrintf("\n");

    CommandPrintf("Service Filter Statistics\n"
                  "-------------------------\n");
    for ( ListIterator_Init(iterator, tsFilter->pidFilters); 
          ListIterator_MoreEntries(iterator); 
          ListIterator_Next(iterator))
    {
        PIDFilter_t *filter = (PIDFilter_t *)ListIterator_Current(iterator);
        if (strcmp(filter->type, ServicePIDFilterType) == 0)
        {
            CommandPrintf("\t%-15s : %lld\n", filter->name, filter->packetsProcessed);
        }
    }
    CommandPrintf("\n");

    CommandPrintf("Other Filter Statistics\n"
                  "------------------------\n");
     for ( ListIterator_Init(iterator, tsFilter->pidFilters); 
           ListIterator_MoreEntries(iterator); 
           ListIterator_Next(iterator))
    {
        PIDFilter_t *filter = (PIDFilter_t *)ListIterator_Current(iterator);
        if ((strcmp(filter->type, PSISIPIDFilterType) != 0) &&
            (strcmp(filter->type, ServicePIDFilterType) != 0))
        {
            CommandPrintf("\t%-15s : %lld (%s)\n", filter->name, filter->packetsFiltered, filter->type);
        }
    }
    CommandPrintf("\n");



    CommandPrintf("Total packets processed: %lld\n", tsFilter->totalPackets);
    CommandPrintf("Approximate TS bitrate : %gMbs\n", ((double)tsFilter->bitrate / (1024.0 * 1024.0)));
}


static void CommandFEStatus(int argc, char **argv)
{
    fe_status_t status;
    unsigned int ber, strength, snr, ucblocks;

    if (DVBFrontEndStatus(MainDVBAdapterGet(), &status, &ber, &strength, &snr, &ucblocks))
    {
        CommandPrintf("Failed to get frontend status!\n");
        return;
    }

    CommandPrintf("Tuner status:  %s%s%s%s%s%s\n",
             (status & FE_HAS_SIGNAL)?"Signal ":"",
             (status & FE_TIMEDOUT)?"Timed out ":"",
             (status & FE_HAS_LOCK)?"Lock ":"",
             (status & FE_HAS_CARRIER)?"Carrier ":"",
             (status & FE_HAS_VITERBI)?"VITERBI ":"",
             (status & FE_HAS_SYNC)?"Sync ":"");
    CommandPrintf("Signal Strength = %d%% SNR = %d%% BER = %x Uncorrected Blocks = %x\n",
        (strength * 100) / 0xffff, (snr * 100) / 0xffff, ber, ucblocks);
}


static void CommandListPids(int argc, char **argv)
{
    Service_t *service;

    service = ServiceFindName(argv[0]);
    if (service)
    {
        bool cached = TRUE;
        int i;
        PIDList_t *pids;
        bool numericOutput = FALSE;
            
        if ((argc == 2) && (strcmp(argv[1], "-n") == 0))
        {
            numericOutput =TRUE;
        }
        
        pids = CachePIDsGet(service);
        if (pids == NULL)
        {
            pids = PIDListGet(service);
            cached = FALSE;
        }

        if (pids)
        {
            CommandPrintf("%d PIDs for \"%s\"%s\n", pids->count, argv[0], cached ? " (Cached)":"");
            for (i = 0; i < pids->count; i ++)
            {
                if (numericOutput)
                {
                    CommandPrintf("%d %d %d\n",pids->pids[i].pid, pids->pids[i].type, pids->pids[i].subType);
                }
                else
                {
                    CommandPrintf("%d %s\n", pids->pids[i].pid, GetStreamTypeString(pids->pids[i].type));
                }

            }

            if (cached)
            {
                CachePIDsRelease();
            }
            else
            {
                PIDListFree(pids);
            }
        }
        else
        {
            CommandPrintf("0 PIDs for \"%s\"\n",argv[0]);
        }
        ServiceRefDec(service);
    }
    else
    {
        CommandError(COMMAND_ERROR_GENERIC, "Service not found!");
    }
}

static char *GetStreamTypeString(int type)
{
    char *result= "Unknown";
    switch(type)
    {
        case 0x00 : result ="ITU-T | ISO/IEC Reserved"; break;
        case 0x01 : result ="ISO/IEC 11172 Video"; break;
        case 0x02 : result ="ITU-T Rec. H.262 | ISO/IEC 13818-2 Video or ISO/IEC 11172-2 constrained parameter video stream"; break;
        case 0x03 : result ="ISO/IEC 11172 Audio"; break;
        case 0x04 : result ="ISO/IEC 13818-3 Audio"; break;
        case 0x05 : result ="ITU-T Rec. H.222.0 | ISO/IEC 13818-1 private_sections"; break;
        case 0x06 : result ="ITU-T Rec. H.222.0 | ISO/IEC 13818-1 PES packets containing private data"; break;
        case 0x07 : result ="ISO/IEC 13522 MHEG"; break;
        case 0x08 : result ="ITU-T Rec. H.222.0 | ISO/IEC 13818-1 Annex A DSM-CC"; break;
        case 0x09 : result ="ITU-T Rec. H.222.1"; break;
        case 0x0A : result ="ISO/IEC 13818-6 type A"; break;
        case 0x0B : result ="ISO/IEC 13818-6 type B"; break;
        case 0x0C : result ="ISO/IEC 13818-6 type C"; break;
        case 0x0D : result ="ISO/IEC 13818-6 type D"; break;
        case 0x0E : result ="ITU-T Rec. H.222.0 | ISO/IEC 13818-1 auxiliary"; break;
        case 0x0F : result ="ISO/IEC 13818-7 Audio with ADTS transport syntax"; break;
        case 0x10 : result ="ISO/IEC 14496-2 Visual"; break;
        case 0x11 : result ="ISO/IEC 14496-3 Audio with the LATM transport syntax as defined in ISO/IEC 14496-3 / AMD 1"; break;
        case 0x12 : result ="ISO/IEC 14496-1 SL-packetized stream or FlexMux stream carried in PES packets"; break;
        case 0x13 : result ="ISO/IEC 14496-1 SL-packetized stream or FlexMux stream carried in ISO/IEC14496_sections."; break;
        case 0x14 : result ="ISO/IEC 13818-6 Synchronized Download Protocol"; break;
        case 0x15 ... 0x7F : result ="ITU-T Rec. H.222.0 | ISO/IEC 13818-1 Reserved"; break;
        case 0x80 ... 0xFF : result ="User Private"; break;
        default:
            break;
    }
    return result;
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

static void CommandVariableFENameGet(char *name)
{

    DVBAdapter_t *adapter = MainDVBAdapterGet();

    CommandPrintf("%s\n", adapter->info.name);
}

static void CommandVariableTSModeGet(char *name)
{

    DVBAdapter_t *adapter = MainDVBAdapterGet();

    CommandPrintf("%s\n", adapter->hardwareRestricted ? "H/W Restricted":"Full TS");
}

