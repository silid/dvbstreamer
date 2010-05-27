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
#include "dvbadapter.h"
#include "ts.h"
#include "logging.h"
#include "cache.h"
#include "main.h"
#include "deliverymethod.h"
#include "plugin.h"
#include "servicefilter.h"
#include "tuning.h"
#include "properties.h"

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
static void CommandFEParams(int argc, char **argv);
static void CommandListProperties(int argc, char **argv);
static void CommandGetProperty(int argc, char **argv);
static void CommandSetProperty(int argc, char **argv);
static void CommandPropertyInfo(int argc, char **argv);
static void CommandDumpTSReader(int argc, char **argv);
static void CommandListLNBs(int argc, char **argv);
static char* GetPropertyTypeString(PropertyType_e type);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
Command_t CommandDetailsInfo[] =
{
    {
        "lsservices",
        0, 6,
        "List all services or for a specific multiplex.",
        "lsservices [-id] [filters] [-q query|[multiplex]]\n"
        "Lists selected services, by default all services on all multiplex are displayed.\n"
        "\n"
        "-id\n"
        "List the services fully quailified id.\n"
        "\n"
        "filters (tv, radio, data, unknown)\n"
        "Multiple filters can be specified or if no filters are specified all selected"
        " services will be displayed\n"
        "\n"
        "-q query\n"
        "List names that match the specified query, %% can be used as a wild card character\n"
        "\n"
        "multiplex (\'mux\'| uid | netid.tsid | frequency)\n"
        "Select only services on the specified multiplex, where \'mux\' indiciated the current multiplex.",
        CommandListServices
    },
    {
        "lsmuxes",
        0, 1,
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
        1, 2,
        "List the PIDs for a specified service.",
        "lspids <service name or service id>\n"
        "List all the PIDs specified in <service name> PMT.",
        CommandListPids
    },    
    {
        "current",
        0, 0,
        "Print out the service currently being streamed.",
        "Shows the service that is currently being streamed to the default output.",
        CommandCurrent
    },
    {
        "serviceinfo",
        1, 1,
        "Display information about a service.",
        "serviceinfo <service name or service id>\n"
        "Displays information about the specified service.",
        CommandServiceInfo
    },
    {
        "muxinfo",
        1, 2,
        "Display information about a mux.",
        "muxinfo <uid> or\n"
        "muxinfo <netid>.<tsid>\n"
        "muxinfo <net id> <ts id>\n"
        "Displays information about the specified service.",
        CommandMuxInfo
    },
    {
        "stats",
        0, 0,
        "Display the stats for the PAT,PMT and service PID filters.",
        "Display the number of packets processed for the PSI/SI filters and the number of"
        " packets filtered for each service filter and manual output.",
        CommandStats
    },
    {
        "festatus",
        0, 0,
        "Displays the status of the tuner.",
        "Displays whether the front end is locked, the bit error rate and signal to noise"
        "ratio and the signal strength",
        CommandFEStatus
    },
    {
        "feparams",
        0, 0,
        "Get current frontend parameters.",
        "Displays the current frontend parameters as a yaml document.",
        CommandFEParams,
    },
    {
        "lsprops",
        0, 2,
        "List available properties.",
        "lsprops [-l] [<property path>]\n"
        "List all available properties at the specified path or the root if not supplied."
        "Use -l to show type and whether the property is readable/writable and has any children.",
        CommandListProperties
    },
    {
        "getprop",
        1, 1,
        "Get the value of a property.",
        "getprop <property path>\n"
        "Get the value of the specified property.",
        CommandGetProperty
    },
    {
        "setprop",
        2, 2,
        "Set the value of a property.",
        "setprop <property path> <new value>\n"
        "Set the value of the specified property to that of <new value>.",
        CommandSetProperty   
    },
    {
        "propinfo",
        1, 1,
        "Display information about a property.",
        "propinfo <property path>\n"
        "Display information about the specified property.",
        CommandPropertyInfo
    },
    {
        "dumptsr",
        0,0,
        "Dump information from the TSReader",
        "Dump information from the TSReader",
        CommandDumpTSReader
    },
    {
        "lslnbs",
        0,0,
        "List known LNBs",
        "List the LNBs that dvbstreamer knows about and the name used to select them",
        CommandListLNBs 
    },
    COMMANDS_SENTINEL
};

static time_t StartTime;
/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
void CommandInstallInfo(void)
{
    CommandRegisterCommands(CommandDetailsInfo);
    StartTime = time(NULL);
}

void CommandUnInstallInfo(void)
{
    CommandUnRegisterCommands(CommandDetailsInfo); 
}

/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/
static void CommandListServices(int argc, char **argv)
{
    List_t *list = NULL;
    Service_t *service;
    Multiplex_t *multiplex = NULL;
    int i;
    bool dvbIds = FALSE;
    char *query = NULL;
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
        else if (strcmp(argv[i], "-q") == 0)
        {
            if (multiplex)
            {
                CommandError(COMMAND_ERROR_GENERIC, "Cannot specify a multiplex and a query string!");
                return;
            }
            if (argc <= i +1)
            {
                CommandError(COMMAND_ERROR_GENERIC, "Missing query string");
                return;
            }
            i ++;
            query = argv[i];
        }
        else if (strcmp(argv[i], "mux") == 0)
        {
            if (query)
            {
                CommandError(COMMAND_ERROR_GENERIC, "Cannot specify a multiplex and a query string!");
                return;
            }
            if (multiplex)
            {
                MultiplexRefDec(multiplex);
            }
            multiplex = TuningCurrentMultiplexGet();
            if (!multiplex)
            {
                CommandError(COMMAND_ERROR_GENERIC, "No multiplex currently selected!");
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
                CommandError(COMMAND_ERROR_GENERIC, "Failed to find multiplex \"%s\"\n", argv[i]);
                return;                    
            }
            
        }
    }

    if (query)
    {
        list = ServiceListForNameLike(query);
    }
    else if (multiplex)
    {
        list = ServiceListForMultiplex(multiplex);
        MultiplexRefDec(multiplex);
    }    
    else
    {
        list = ServiceListAll();
    }

    if (list != NULL)
    {
        ListIterator_t iterator;
        for (ListIterator_Init(iterator, list); 
             ListIterator_MoreEntries(iterator);
             ListIterator_Next(iterator))
        {
            service = ListIterator_Current(iterator);
            if (FilterService(service, filterByType, filterByAccess, provider))
            {
                if (dvbIds)
                {
                    char *idName = ServiceGetIDNameStr(service, NULL);
                    CommandPrintf("%s\n", idName); 
                    free(idName);
                }
                else
                {
                    CommandPrintf("%s\n", service->name);
                }
            }

        }
        ObjectListFree(list);
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
    int i;
    MultiplexList_t *list;
    Multiplex_t *multiplex = NULL;
    bool ids = FALSE;
    
    if ((argc == 1) && (strcmp(argv[0], "-id") == 0))
    {
        ids = TRUE;
    }

    list = MultiplexGetAll();
    for (i = 0; i < list->nrofMultiplexes; i ++)
    {
        multiplex = (Multiplex_t*)list->multiplexes[i];
        if (ids)
        {
            CommandPrintf("%04x.%04x : %d \n", 
                multiplex->networkId & 0xffff, multiplex->tsId & 0xffff, multiplex->uid);
        }
        else
        {
            CommandPrintf("%d\n", multiplex->uid);
        }
    }
    ObjectRefDec(list);
}

static void CommandCurrent(int argc, char **argv)
{
    Service_t *service = TuningCurrentServiceGet();
    if ( service)
    {
        char *idName = ServiceGetIDNameStr(service, NULL);
        CommandPrintf("%s\n", idName); 
        free(idName);
        ServiceRefDec(service);
    }
}
static void CommandServiceInfo(int argc, char **argv)
{
    Service_t *service;

    UpdateDatabase();
    
    service = ServiceFind(argv[0]);

    if (service)
    {
        static const char *serviceType[]= {"Digital TV", "Digital Radio", "Data", "Unknown"};
        CommandPrintf("Name                : \"%s\"\n", service->name);
        CommandPrintf("Provider            : \"%s\"\n", service->provider);
        CommandPrintf("Type                : %s\n", serviceType[service->type]);
        CommandPrintf("Conditional Access? : %s\n", service->conditionalAccess ? "CA":"Free to Air");
        CommandPrintf("ID                  : %04x.%04x.%04x\n", service->networkId, service->tsId, service->id);
        CommandPrintf("Multiplex UID       : %d\n", service->multiplexUID);
        CommandPrintf("Source              : 0x%04x\n", service->source);
        CommandPrintf("Default Authority   : \"%s\"\n", service->defaultAuthority);
        CommandPrintf("PMT PID             : 0x%04x\n", service->pmtPID);
        ServiceRefDec(service);
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
        char *line;
        CommandPrintf("UID                 : %d\n", multiplex->uid);
        CommandPrintf("ID                  : %04x.%04x\n", multiplex->networkId, multiplex->tsId);
        CommandPrintf("PAT Version         : %d\n", multiplex->patVersion);
        CommandPrintf("Tuning Parameters: \n");
        CommandPrintf("    Type: %s\n", DVBDeliverySystemStr[multiplex->deliverySystem]);
        line = strtok(multiplex->tuningParams, "\n");
        while (line)
        {
            CommandPrintf("    %s\n", line);
            line = strtok(NULL, "\n");
        }
        MultiplexRefDec(multiplex);
    }
    else
    {
        CommandError(COMMAND_ERROR_GENERIC, "Multiplex not found!");
    }
}


static void CommandStats(int argc, char **argv)
{
    TSReader_t *tsReader = MainTSReaderGet();
    TSReaderStats_t *stats = TSReaderExtractStats(tsReader);
    TSFilterGroupTypeStats_t *typeStats;
    for (typeStats = stats->types; typeStats; typeStats = typeStats->next)
    {
        TSFilterGroupStats_t *groupStats;
        CommandPrintf("%s: \n", typeStats->type);
        for (groupStats = typeStats->groups; groupStats; groupStats = groupStats->next)
        {
            CommandPrintf("    %20s : %lld (%lld)\n", groupStats->name, groupStats->packetsProcessed, groupStats->sectionsProcessed);
        }
        CommandPrintf("\n");
    }
    CommandPrintf("Total packets processed: %lld\n", stats->totalPackets);
    CommandPrintf("Approximate TS bitrate : %gMbs\n", ((double)stats->bitrate / (1024.0 * 1024.0)));
    ObjectRefDec(stats);
}


static void CommandFEStatus(int argc, char **argv)
{
    DVBFrontEndStatus_e status;
    unsigned int ber, strength, snr, ucblocks;

    if (DVBFrontEndStatus(MainDVBAdapterGet(), &status, &ber, &strength, &snr, &ucblocks))
    {
        CommandPrintf("Failed to get frontend status!\n");
        return;
    }
    CommandPrintf("Tuner status: [ %s%s%s%s%s%s ]\n",
             (status & FESTATUS_HAS_SIGNAL)?"Signal, ":"",
             (status & FESTATUS_TIMEDOUT)?"Timed out, ":"",
             (status & FESTATUS_HAS_LOCK)?"Lock, ":"",
             (status & FESTATUS_HAS_CARRIER)?"Carrier, ":"",
             (status & FESTATUS_HAS_VITERBI)?"VITERBI, ":"",
             (status & FESTATUS_HAS_SYNC)?"Sync ":"");
    CommandPrintf("Signal Strength: %d%%\nSNR: %d%%\nBER: %d\nUncorrected Blocks: %d\n",
        (strength * 100) / 0xffff, (snr * 100) / 0xffff, ber, ucblocks);
}

static void CommandFEParams(int argc, char **argv)
{
    DVBAdapter_t *adapter = MainDVBAdapterGet();
    DVBDeliverySystem_e system;
    char *params = DVBFrontEndParametersGet(adapter,&system);
    CommandPrintf("Delivery System: %s\n", DVBDeliverySystemStr[system]);
    CommandPrintf("%s\n", params);
    free(params);
}

static void CommandListPids(int argc, char **argv)
{
    Service_t *service;

    service = ServiceFind(argv[0]);
    if (service)
    {
        bool cached = TRUE;
        int i;
        ProgramInfo_t *info;
        bool numericOutput = FALSE;
            
        if ((argc == 2) && (strcmp(argv[1], "-n") == 0))
        {
            numericOutput =TRUE;
        }
        
        info = CacheProgramInfoGet(service);
        if (info == NULL)
        {
            info = ProgramInfoGet(service);
            cached = FALSE;
        }

        if (info)
        {
            bool pcrPresent = FALSE;
            CommandPrintf("%d PIDs for \"%s\"%s\n", info->streamInfoList->nrofStreams, argv[0], cached ? " (Cached)":"");
            for (i = 0; i < info->streamInfoList->nrofStreams; i ++)
            {
                if (info->streamInfoList->streams[i].pid == info->pcrPID)
                {
                    pcrPresent = TRUE;
                }
                if (numericOutput)
                {
                    CommandPrintf("%4d: { type: %d }\n",info->streamInfoList->streams[i].pid, info->streamInfoList->streams[i].type);
                }
                else
                {
                    CommandPrintf("%4d: { type: \"%s\" }\n", info->streamInfoList->streams[i].pid, GetStreamTypeString(info->streamInfoList->streams[i].type));
                }

            }
            if (!pcrPresent)
            {
                if (numericOutput)
                {
                    CommandPrintf("%4d: { type: -1 }\n",info->pcrPID);
                }
                else
                {
                    CommandPrintf("%4d: { type: PCR }\n", info->pcrPID);
                }
            }
            ObjectRefDec(info);

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
        case 0x15 : result ="Metadata carried in PES packets"; break;
        case 0x16 : result ="Metadata carried in metadata_sections"; break;
        case 0x17 : result ="Metadata carried in ISO/IEC 13818-6 Data Carousel"; break;
        case 0x18 : result ="Metadata carried in ISO/IEC 13818-6 Object Carousel"; break;
        case 0x19 : result ="Metadata carried in ISO/IEC 13818-6 Synchronized Download Protocol"; break;
        case 0x1A : result ="IPMP stream (defined in ISO/IEC 13818-11, MPEG-2 IPMP)"; break;
        case 0x1B : result ="AVC video stream as defined in ITU-T Rec. H.264 | ISO/IEC 14496-10 Video"; break;       
        case 0x1C ... 0x7E : result ="ITU-T Rec. H.222.0 | ISO/IEC 13818-1 Reserved"; break;
        case 0x7F : result ="IPMP stream"; break;
        case 0x80 ... 0xFF : result ="User Private"; break;
        default:
            break;
    }
    return result;
}

static void CommandListProperties(int argc, char **argv)
{
    PropertiesEnumerator_t pos;
    PropertyInfo_t propInfo;
    char *path = NULL;
    char *typeStr;
    int i;
    bool list = FALSE;
    for (i = 0; i < argc; i++)
    {
        if (strcmp("-l", argv[i]) == 0)
        {
            list = TRUE;
        }
        else if (path == NULL)
        {
            path = argv[i];
        }
    }
    if (PropertiesEnumerate(path, &pos) == 0)
    {
        if (PropertiesEnumMoreEntries(pos))
        {
            for (; PropertiesEnumMoreEntries(pos); pos = PropertiesEnumNext(pos))
            {
                PropertiesEnumGetInfo(pos, &propInfo);
                if (list)
                {
                    typeStr = GetPropertyTypeString(propInfo.type);
                    CommandPrintf("%c%c%c %-10s %s\n", propInfo.hasChildren == TRUE ? 'D':'-', 
                                                       propInfo.readable    == TRUE ? 'R':'-', 
                                                       propInfo.writeable   == TRUE ? 'W':'-', 
                                                       typeStr, propInfo.name);
                }
                else
                {
                    CommandPrintf("%s\n", propInfo.name);
                }
            }
        }
        else
        {
            CommandError(COMMAND_ERROR_GENERIC, "Property %s does not have any children!", path);
        }
    }
    else
    {
        CommandError(COMMAND_ERROR_GENERIC, "Couldn\'t find property \"%s\"", path);
    }
    
}

static  void CommandGetProperty(int argc, char **argv)
{
    PropertyValue_t value;
    
    if (PropertiesGet(argv[0], &value) == 0)
    {
        switch(value.type)
        {
            case PropertyType_Int:
                CommandPrintf("%d\n", value.u.integer);
                break;                    
            case PropertyType_Float:
                CommandPrintf("%lf\n", value.u.fp);
                break;           
            case PropertyType_Boolean:
                CommandPrintf("%s\n", value.u.boolean ? "True":"False");
                break;
            case PropertyType_String:
                CommandPrintf("%s\n", value.u.string);
                free(value.u.string);
                break;
            case PropertyType_Char:
                CommandPrintf("%c\n", value.u.ch);
                break;
            case PropertyType_PID:
                CommandPrintf("%u\n", value.u.pid);
                break;
            case PropertyType_IPAddress:
                CommandPrintf("%s\n", value.u.string);
                break;
            default:
                break;
        }
    }
}

static void CommandSetProperty(int argc, char **argv)
{
    CommandCheckAuthenticated();
    if (PropertiesSetStr(argv[0], argv[1]) != 0)
    {
        CommandError(COMMAND_ERROR_GENERIC, "Failed to set property \"%s\"", argv[0]);
    }
}

static void CommandPropertyInfo(int argc, char **argv)
{
    PropertyInfo_t propInfo;
    
    if (PropertiesGetInfo(argv[0], &propInfo) == 0)
    {
        CommandPrintf("Type         : %s\n", GetPropertyTypeString(propInfo.type));
        CommandPrintf("Readable     : %s\n", propInfo.readable    == TRUE ? "Yes":"No");
        CommandPrintf("Writeable    : %s\n", propInfo.writeable   == TRUE ? "Yes":"No");
        CommandPrintf("Has Children : %s\n", propInfo.hasChildren == TRUE ? "Yes":"No");
        CommandPrintf("Description  : |\n    %s\n", propInfo.desc == NULL ? "":propInfo.desc);
    }
    else
    {
        CommandError(COMMAND_ERROR_GENERIC, "Couldn\'t find property \"%s\"", argv[0]);
    }
}

static void CommandDumpTSReader(int argc, char **argv)
{
    TSReader_t *reader = MainTSReaderGet();
    ListIterator_t iterator;
    int p;
    int count = 0;
    TSReaderLock(reader);

    for (p = 0; p < TSREADER_NROF_FILTERS; p ++)
    {
        count += reader->packetFilters[p] == NULL ? 0:1;
    }
    
    CommandPrintf("PID Filters (%d)\n",count);
    for (p = 0; p < TSREADER_NROF_FILTERS; p ++)
    {
        TSPacketFilter_t *filter;
        if (reader->packetFilters[p] == NULL)
        {
            continue;
        }
        CommandPrintf("    0x%04x : ", p);
        for (filter = reader->packetFilters[p]; filter; filter = filter->flNext)
        {
            if (filter->group)
            {
                CommandPrintf("\"%s\"", filter->group->name);
            }
            else
            {
                CommandPrintf("<Section Filter>");
            }
            if (filter->flNext)
            {
                CommandPrintf(", ");
            }
            else
            {
                CommandPrintf("\n");
            }
        }
        
    }
    CommandPrintf("Section filters - Active (%d)\n", ListCount(reader->activeSectionFilters));
    ListIterator_ForEach(iterator, reader->activeSectionFilters)
    {
        ListIterator_t iterator_sf;
        TSSectionFilterList_t *sfList = ListIterator_Current(iterator);
        TSSectionFilter_t *sf;
        CommandPrintf("    0x%04x\n", sfList->pid);
        ListIterator_ForEach(iterator_sf, sfList->filters)
        {
            sf = ListIterator_Current(iterator_sf);
            CommandPrintf("        %s\n", sf->group->name);
        }
    }
    CommandPrintf("Section filters - Awaiting scheduling (%d)\n", ListCount(reader->sectionFilters));
    ListIterator_ForEach(iterator, reader->sectionFilters)
    {
        ListIterator_t iterator_sf;
        TSSectionFilterList_t *sfList = ListIterator_Current(iterator);
        TSSectionFilter_t *sf;
        CommandPrintf("    0x%04x\n", sfList->pid);
        ListIterator_ForEach(iterator_sf, sfList->filters)
        {
            sf = ListIterator_Current(iterator_sf);
            CommandPrintf("        %s\n", sf->group->name);
        }
    }
    TSReaderUnLock(reader);
}

static void CommandListLNBs(int argc, char **argv)
{
    LNBInfo_t *knownLNB;
    int i = 0;
    for (i = 0; (knownLNB = LNBEnumerate(i)); i ++)
    {
        char **desclines;
        CommandPrintf("%s :\n", knownLNB->name);

        for (desclines = knownLNB->desc; *desclines; desclines ++)
        {
            CommandPrintf("   %s\n", *desclines);
        }
        CommandPrintf("\n");
    }
}

static char* GetPropertyTypeString(PropertyType_e type)
{
    char *typeStr = NULL;
    switch(type)
    {
        case PropertyType_None:
            typeStr = "None";
            break;
        case PropertyType_Int:
            typeStr = "Integer";
            break;                    
        case PropertyType_Float:
            typeStr = "Float";
            break;           
        case PropertyType_Boolean:
            typeStr = "Boolean";
            break;
        case PropertyType_String:
            typeStr = "String";
            break;
        case PropertyType_Char:
            typeStr = "Character";
            break;
        case PropertyType_PID:
            typeStr = "PID";
            break;
        case PropertyType_IPAddress:
            typeStr = "IP Address";
            break;
        default:
            typeStr = "Unknown";
            break;
    }
    return typeStr;
}


