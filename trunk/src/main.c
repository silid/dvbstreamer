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
 
main.c
 
Entry point to the application.
 
*/
#include "config.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <getopt.h>
#include <ctype.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "parsezap.h"
#include "dbase.h"
#include "multiplexes.h"
#include "services.h"
#include "dvb.h"
#include "ts.h"
#include "udpoutput.h"
#include "main.h"
#include "patprocessor.h"
#include "pmtprocessor.h"
#include "sdtprocessor.h"
#include "cache.h"
#include "logging.h"

#define PROMPT "DVBStream>"

#define MAX_OUTPUTS (MAX_FILTERS - PIDFilterIndex_Count)

enum PIDFilterIndex
{
    PIDFilterIndex_PAT = 0,
    PIDFilterIndex_PMT,
    PIDFilterIndex_SDT,
    PIDFilterIndex_Service ,


    PIDFilterIndex_Count
};

static char *PIDFilterNames[] = {
                                    "PAT",
                                    "PMT",
                                    "SDT",
                                    "Service",
                                };

typedef struct Output_t
{
    char *name;
    PIDFilter_t *filter;
    PIDFilterSimpleFilter_t pids;
}
Output_t;

typedef struct Command_t
{
    char *command;
    char *shorthelp;
    char *longhelp;
    void (*commandfunc)(char *argument);
}
Command_t;


static int ServiceFilterPacket(void *arg, uint16_t pid, TSPacket_t *packet);

static void usage(char *appname);
static void version(void);

static void CommandLoop(void);
static int ProcessFile(char *file);
static void GetCommand(char **command, char **argument);
static int ProcessCommand(char *command, char *argument);
static void ParseLine(char *line, char **command, char **argument);

static void CommandQuit(char *argument);
static void CommandServices(char *argument);
static void CommandMultiplex(char *argument);
static void CommandSelect(char *argument);
static void CommandPids(char *argument);
static void CommandStats(char *argument);
static void CommandAddOutput(char *argument);
static void CommandRmOutput(char *argument);
static void CommandOutputs(char *argument);

static void CommandAddPID(char *argument);
static void CommandRmPID(char *argument);
static void CommandOutputPIDs(char *argument);

static Output_t *ParseOutputPID(char *argument, uint16_t *pid);
static Output_t *FindOutput(char *name);
static void CommandHelp(char *argument);

volatile Multiplex_t *CurrentMultiplex = NULL;
volatile Service_t *CurrentService = NULL;

static int quit = 0;
static DVBAdapter_t *adapter;
static TSFilter_t *tsfilter;
static PIDFilter_t *pidfilters[PIDFilterIndex_Count];
static Output_t outputs[MAX_OUTPUTS];

static Command_t commands[] = {
                                  {
                                      "quit",
                                      "Exit the program",
                                      "Exit the program, can be used in the startup file to stop further processing.",
                                      CommandQuit
                                  },
                                  {
                                      "services",
                                      "List all available services",
                                      "Lists all the services currently in the database. This list will be "
                                      "updated as updates to the PAT are detected",
                                      CommandServices
                                  },
                                  {
                                      "multiplex",
                                      "List all the services on the current multiplex",
                                      "List only the services on the same multiplex as the currently selected service",
                                      CommandMultiplex,
                                  },
                                  {
                                      "select",
                                      "Select a new service to stream",
                                      "select <service name>\n"
                                      "Sets <service name> as the current service, this may mean tuning to a different"
                                      "multiplex.",
                                      CommandSelect
                                  },
                                  {
                                      "pids",
                                      "List the PIDs for a specified service",
                                      "pids <service name>\n"
                                      "List the PIDs for <service name>.",
                                      CommandPids
                                  },
                                  {
                                      "stats",
                                      "Display the stats for the PAT,PMT and service PID filters",
                                      "Display the number of packets processed and the number of packets"
                                      " filtered by each filter.",
                                      CommandStats
                                  },
                                  {
                                      "addoutput",
                                      "Add a new destination for manually filtered PIDs.",
                                      "addoutput <output name> <ipaddress>:<udp port>\n"
                                      "Adds a new destination for sending packets to. This is only used for"
                                      " manually filtered packets."
                                      "To send packets to this destination you'll need to also call \'filterpid\' "
                                      "with this output as an argument.",
                                      CommandAddOutput
                                  },
                                  {
                                      "rmoutput",
                                      "Remove a destination for manually filtered PIDs.",
                                      "rmoutput <output name>\n"
                                      "Removes the destination and stops all filters associated with this output.",
                                      CommandRmOutput
                                  },
                                  {
                                      "outputs",
                                      "List current outputs",
                                      "List all active additonal output names and destinations.",
                                      CommandOutputs
                                  },
                                  {
                                      "addpid",
                                      "Adds a PID to filter to an output",
                                      "addpid <output name> <pid>\n"
                                      "Adds a PID to the filter to be sent to the specified output.",
                                      CommandAddPID
                                  },
                                  {
                                      "rmpid",
                                      "Removes a PID to filter from an output",
                                      "rmpid <output name> <pid>\n"
                                      "Removes the PID from the filter that is sending packets to the specified output.",
                                      CommandRmPID
                                  },
                                  {
                                      "outputpids",
                                      "List PIDs for output",
                                      "List the PIDs being filtered for a specific output",
                                      CommandOutputPIDs
                                  },
                                  {
                                      "help",
                                      "Display the list of commands or help on a specific command",
                                      "help <command>\n"
                                      "Displays help for the specified command.",
                                      CommandHelp
                                  },
                                  {NULL,NULL,NULL}
                              };


int main(int argc, char *argv[])
{
    char *startupFile = NULL;
    fe_type_t channelsFileType = FE_OFDM;
    char channelsFile[PATH_MAX];
    void *outputArg = NULL;
    int i;
    int adapterNumber = 0;
    PIDFilterSimpleFilter_t patsimplefilter;
    PIDFilterSimpleFilter_t sdtsimplefilter;
    void *patprocessor;
    void *pmtprocessor;
    void *sdtprocessor;

    channelsFile[0] = 0;

    while (1)
    {
        char c;
        c = getopt(argc, argv, "vVo:a:t:s:c:f:");
        if (c == -1)
        {
            break;
        }
        switch (c)
        {
                case 'v': verbosity ++;
                break;
                case 'V':
                version();
                exit(0);
                break;
                case 'o':
                outputArg = UDPOutputCreate(optarg);
                if (outputArg == NULL)
                {
                    printlog(LOG_ERROR, "Error creating UDP output!\n");
                    exit(1);
                }
                printlog(LOG_INFOV, "Output will be via UDP to %s\n", optarg);
                break;
                case 'a': adapterNumber = atoi(optarg);
                printlog(LOG_INFOV, "Using adapter %d\n", adapterNumber);
                break;
                case 'f': startupFile = strdup(optarg);
                printlog(LOG_INFOV, "Using startup script %s\n", startupFile);
                break;
                /* Database initialisation options*/
                case 't':
                strcpy(channelsFile, optarg);
                channelsFileType = FE_OFDM;
                printlog(LOG_INFOV, "Using DVB-T channels file %s\n", channelsFile);
                break;
                case 's':
                strcpy(channelsFile, optarg);
                channelsFileType = FE_QPSK;
                printlog(LOG_INFOV, "Using DVB-S channels file %s\n", channelsFile);
                break;
                case 'c':
                strcpy(channelsFile, optarg);
                channelsFileType = FE_QAM;
                printlog(LOG_INFOV, "Using DVB-C channels file %s\n", channelsFile);
                break;
                default:
                usage(argv[0]);
                exit(1);
        }
    }
    if (outputArg == NULL)
    {
        printlog(LOG_ERROR, "No output set!\n");
        usage(argv[0]);
        exit(1);
    }

    if (DBaseInit(adapterNumber))
    {
        printlog(LOG_ERROR, "Failed to initialise database\n");
        exit(1);
    }
    printlog(LOG_DEBUGV, "Database initalised\n");

    if (CacheInit())
    {
        printlog(LOG_ERROR,"Failed to initialise cache\n");
        exit(1);
    }
    printlog(LOG_DEBUGV, "Cache initalised\n");

    if (strlen(channelsFile))
    {
        printlog(LOG_INFO,"Importing services from %s\n", channelsFile);
        if (!parsezapfile(channelsFile, channelsFileType))
        {
            exit(1);
        }
        printlog(LOG_DEBUGV, "Channels file imported\n");
    }

    printlog(LOG_INFO, "%d Services available on %d Multiplexes\n", ServiceCount(), MultiplexCount());

    /* Initialise the DVB adapter */
    adapter = DVBInit(adapterNumber);
    if (!adapter)
    {
        printlog(LOG_ERROR, "Failed to open DVB adapter!\n");
        exit(1);
    }
    printlog(LOG_DEBUGV, "DVB adapter initalised\n");

    DVBDemuxStreamEntireTSToDVR(adapter);
    printlog(LOG_DEBUGV, "Streaming complete TS to DVR done\n");

    /* Create Transport stream filter thread */
    tsfilter = TSFilterCreate(adapter);
    if (!tsfilter)
    {
        printlog(LOG_ERROR, "Failed to create filter!\n");
        exit(1);
    }
    printlog(LOG_DEBUGV, "TSFilter created\n");

    /* Create PAT filter */
    patsimplefilter.pidcount = 1;
    patsimplefilter.pids[0] = 0;
    patprocessor = PATProcessorCreate();
    pidfilters[PIDFilterIndex_PAT] = PIDFilterSetup(tsfilter,
                                     PIDFilterSimpleFilter, &patsimplefilter,
                                     PATProcessorProcessPacket, patprocessor,
                                     UDPOutputPacketOutput,outputArg);

    /* Create PMT filter */
    pmtprocessor = PMTProcessorCreate();
    pidfilters[PIDFilterIndex_PMT] = PIDFilterSetup(tsfilter,
                                    PMTProcessorFilterPacket, NULL,
                                    PMTProcessorProcessPacket, pmtprocessor,
                                    UDPOutputPacketOutput,outputArg);

    /* Create Service filter */
    pidfilters[PIDFilterIndex_Service] = PIDFilterSetup(tsfilter,
                                        ServiceFilterPacket, NULL,
                                        NULL, NULL,
                                        UDPOutputPacketOutput,outputArg);

    /* Create SDT Filter */
    sdtsimplefilter.pidcount = 1;
    sdtsimplefilter.pids[0] = 0x11;
    sdtprocessor = SDTProcessorCreate();
    pidfilters[PIDFilterIndex_SDT] = PIDFilterSetup(tsfilter,
                                    PIDFilterSimpleFilter, &sdtsimplefilter,
                                    SDTProcessorProcessPacket, sdtprocessor,
                                    NULL,NULL);
    /* Enable all the filters */
    for (i = 0; i < PIDFilterIndex_Count; i ++)
    {
        pidfilters[i]->enabled = 1;
    }
    printlog(LOG_DEBUGV, "PID filters started\n");

    /* Clear all outputs */
    memset(&outputs, 0, sizeof(outputs));

    if (startupFile)
    {
        if (ProcessFile(startupFile))
        {
            printlog(LOG_ERROR, "%s not found!\n", startupFile);
        }
        free(startupFile);
    }
    printlog(LOG_DEBUGV, "Startup file processed\n");

    CommandLoop();
    printlog(LOG_DEBUGV, "Command loop finished, shutting down\n");

    /* Disable all the filters */
    for (i = 0; i < PIDFilterIndex_Count; i ++)
    {
        pidfilters[i]->enabled = 0;
    }
    printlog(LOG_DEBUGV, "PID filters stopped\n");

    PATProcessorDestroy( patprocessor);
    PMTProcessorDestroy( pmtprocessor);
    SDTProcessorDestroy( sdtprocessor);
    printlog(LOG_DEBUGV, "Processors destroyed\n");
    /* Close the adapter and shutdown the filter etc*/
    DVBDispose(adapter);
    printlog(LOG_DEBUGV, "DVB Adapter shutdown\n");

    TSFilterDestroy(tsfilter);
    printlog(LOG_DEBUGV, "TSFilter destroyed\n");

    UDPOutputClose(outputArg);
    printlog(LOG_DEBUGV, "UDPOutput closed\n");

    CacheDeInit();
    printlog(LOG_DEBUGV, "Cache deinitalised\n");
    
    DBaseDeInit();
    printlog(LOG_DEBUGV, "Database deinitalised\n");
    return 0;
}


static int ServiceFilterPacket(void *arg, uint16_t pid, TSPacket_t *packet)
{
    int i;
    if (CurrentService)
    {
        int count;
        PID_t *pids;
        pids = CachePIDsGet((Service_t *)CurrentService, &count);
        for (i = 0; i < count; i ++)
        {
            if (pid == pids[i].pid)
            {
                return 1;
            }
        }
    }
    return 0;
}

/*
 * Find the service named <name> and tune to the new frequency for the multiplex the service is
 * on (if required) and then select the new service id to filter packets for.
 */
Service_t *SetCurrentService(DVBAdapter_t *adapter, TSFilter_t *tsfilter, char *name)
{
    Multiplex_t *multiplex;
    Service_t *service;

    pthread_mutex_lock(&tsfilter->mutex);
    printlog(LOG_DEBUG,"Writing changes back to database.\n");
    CacheWriteback();
    pthread_mutex_unlock(&tsfilter->mutex);

    service = CacheServiceFindName(name, &multiplex);
    if (!service)
    {
        return NULL;
    }

    printlog(LOG_DEBUG, "Service found id:0x%04x Multiplex:%d\n", service->id, service->multiplexfreq);
    if ((CurrentService == NULL) || (!ServiceAreEqual(service,CurrentService)))
    {
        int i;

        printlog(LOG_DEBUGV,"Disabling filters\n");
        TSFilterEnable(tsfilter, 0);
        pthread_mutex_lock(&tsfilter->mutex);

        if (CurrentMultiplex)
        {
            printlog(LOG_DEBUG,"Current Multiplex frequency = %d TS id = %d\n",CurrentMultiplex->freq, CurrentMultiplex->tsid);
        }
        else
        {
            printlog(LOG_DEBUG,"No current Multiplex!\n");
        }

        if (multiplex)
        {
            printlog(LOG_DEBUG,"New Multiplex frequency =%d TS id = %d\n",multiplex->freq, multiplex->tsid);
        }
        else
        {
            printlog(LOG_DEBUG,"No new Multiplex!\n");
        }

        if ((CurrentMultiplex!= NULL) && MultiplexAreEqual(multiplex, CurrentMultiplex))
        {
            printlog(LOG_DEBUGV,"Same multiplex\n");
            CurrentService = service;
        }
        else
        {
            struct dvb_frontend_parameters feparams;
            if (CurrentMultiplex)
            {
                free((void*)CurrentMultiplex);
            }

            printlog(LOG_DEBUGV,"Caching Services\n");
            CacheLoad(multiplex);
            CurrentMultiplex = multiplex;

            printlog(LOG_DEBUGV,"Getting Frondend parameters\n");
            MultiplexFrontendParametersGet((Multiplex_t*)CurrentMultiplex, &feparams);

            printlog(LOG_DEBUGV,"Tuning\n");
            DVBFrontEndTune(adapter, &feparams);

            CurrentService = CacheServiceFindId(service->id);
            ServiceFree(service);
        }

        /* Clear all filter stats */
        tsfilter->totalpackets = 0;
        for (i = 0; i < PIDFilterIndex_Count; i ++)
        {
            pidfilters[i]->packetsfiltered  = 0;
            pidfilters[i]->packetsprocessed = 0;
            pidfilters[i]->packetsoutput    = 0;
        }

        for (i = 0; i < MAX_OUTPUTS; i ++)
        {
            if (!outputs[i].name)
            {
                continue;
            }
            outputs[i].filter->packetsfiltered = 0;
            outputs[i].filter->packetsoutput   = 0;
        }
        pthread_mutex_unlock(&tsfilter->mutex);
        printlog(LOG_DEBUGV,"Enabling filters\n");
        TSFilterEnable(tsfilter, 1);
    }

    return (Service_t*)CurrentService;
}

/*
 * Output command line usage and help.
 */
static void usage(char *appname)
{
    fprintf(stderr,"Usage:%s <options>\n"
            "      Options:\n"
            "      -v            : Increase the amount of debug output, can be used multiple\n"
            "                      times for more output\n"
            "      -V            : Print version information then exit\n"
            "      -o <host:port>: Output transport stream via UDP to the given host and port\n"
            "      -a <adapter>  : Use adapter number (ie /dev/dvb/adapter<adapter>/...)\n"
            "      -f <file>     : Run startup script file before starting the command prompt\n"
            "      -t <file>     : Terrestrial channels.conf file to import services and \n"
            "                      multiplexes from.\n"
            "      -s <file>     : Satellite channels.conf file to import services and \n"
            "                      multiplexes from.(EXPERIMENTAL)\n"
            "      -c <file>     : Cable channels.conf file to import services and \n"
            "                      multiplexes from.(EXPERIMENTAL)\n",
            appname
           );
}

/*
 * Output version and license conditions
 */
static void version(void)
{
    printf("%s - %s\n"
           "Written by Adam Charrett (charrea6@sourceforge.net).\n"
           "\n"
           "Copyright 2006 Adam Charrett\n"
           "This is free software; see the source for copying conditions.  There is NO\n"
           "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n",
           PACKAGE, VERSION);
}


/**************** Command Loop/Startup file functions ************************/
static void CommandLoop(void)
{
    char *command;
    char *argument;
    quit = 0;
    /* Start Command loop */
    while(!quit)
    {
        GetCommand(&command, &argument);

        if (command)
        {
            if (!ProcessCommand(command, argument))
            {
                printf("Unknown command \"%s\"\n", command);
            }
            free(command);
            if (argument)
            {
                free(argument);
            }
        }
    }
}

static int ProcessFile(char *file)
{
    int lineno = 0;
    FILE *fp;
    char *command;
    char *argument;
    char line[256];
    char *nl;

    fp = fopen(file, "r");
    if (!fp)
    {
        return 1;
    }

    quit = 0;
    while(!feof(fp) && !quit)
    {
        fgets(line, sizeof(line), fp);
        nl = strchr(line, '\n');
        if (nl)
        {
            *nl = 0;
        }
        nl = strchr(line, '\r');
        if (nl)
        {
            *nl = 0;
        }
        lineno ++;
        ParseLine(line, &command, &argument);
        if (command && (strlen(command) > 0))
        {
            if (!ProcessCommand(command, argument))
            {
                printf("%s(%d): Unknown command \"%s\"\n", file, lineno, command);
            }
            free(command);
            if (argument)
            {
                free(argument);
            }
        }

    }

    fclose(fp);
    return 0;
}
/*************** Command parsing functions ***********************************/
static void GetCommand(char **command, char **argument)
{
    char *line = readline(PROMPT);
    *command = NULL;
    *argument = NULL;

    /* If the user has entered a non blank line process it */
    if (line && line[0])
    {
        add_history(line);
        ParseLine(line, command, argument);
    }

    /* Make sure we free the line buffer */
    if (line)
    {
        free(line);
    }
}


static int ProcessCommand(char *command, char *argument)
{
    int i;
    int commandFound = 0;
    for (i = 0; commands[i].command; i ++)
    {
        if (strcasecmp(command,commands[i].command) == 0)
        {
            commands[i].commandfunc(argument);
            commandFound = 1;
            break;
        }
    }
    return commandFound;
}

static void ParseLine(char *line, char **command, char **argument)
{
    char *space;
    char *hash;

    *command = NULL;
    *argument = NULL;

    /* Trim leading spaces */
    for (;*line && isspace(*line); line ++);

    /* Handle null/comment lines */
    if (*line == '#')
    {
        return;
    }

    /* Handle end of line comments */
    hash = strchr(line, '#');
    if (hash)
    {
        *hash = 0;
    }

    /* Find first space after command */
    space = strchr(line, ' ');
    if (space)
    {
        *space = 0;
        *argument = strdup(space + 1);
    }
    *command = strdup(line);
}

char *Trim(char *str)
{
    char *result;
    char *end;
    /* Trim spaces from the start of the address */
    for(result = str; *result && isspace(*result); result ++);

    /* Trim spaces from the end of the address */
    for (end = result + (strlen(result) - 1); (result != end) && isspace(*end); end --)
    {
        *end = 0;
    }
    return result;
}
/************************** Command Functions ********************************/
static void CommandQuit(char *argument)
{
    quit = 1;
}

static void CommandServices(char *argument)
{
    ServiceEnumerator_t enumerator = ServiceEnumeratorGet();
    Service_t *service;
    do
    {
        service = ServiceGetNext(enumerator);
        if (service)
        {
            printf("%4x: %s\n", service->id, service->name);
            ServiceFree(service);
        }
    }
    while(service);
}

static void CommandMultiplex(char *argument)
{
    if (CurrentMultiplex == NULL)
    {
        printf("No multiplex currently selected!\n");
    }
    else
    {
        ServiceEnumerator_t enumerator = ServiceEnumeratorForMultiplex(CurrentMultiplex->freq);
        Service_t *service;
        do
        {
            service = ServiceGetNext(enumerator);
            if (service)
            {
                printf("%4x: %s\n", service->id, service->name);
                ServiceFree(service);
            }
        }
        while(service);
    }
}

static void CommandSelect(char *argument)
{
    Service_t *service;
    if (argument == NULL)
    {
        printf("No service specified!\n");
        return;
    }

    service = SetCurrentService(adapter, tsfilter, argument);
    if (service)
    {
        printf("Name      = %s\n", service->name);
        printf("ID        = %04x\n", service->id);
    }
    else
    {
        printf("Could not find \"%s\"\n", argument);
    }
}

static void CommandPids(char *argument)
{
    Service_t *service;

    if (argument == NULL)
    {
        printf("No service specified!\n");
        return;
    }

    service = ServiceFindName(argument);
    if (service)
    {
        int i;
        int count;
        PID_t *pids;
        count = ServicePIDCount(service);
        if (count > 0)
        {
            printf("PIDs for \"%s\"\n", argument);
            pids = calloc(count, sizeof(PID_t));
            if (pids)
            {
                ServicePIDGet(service, pids, &count);
                for (i = 0; i < count; i ++)
                {
                    printf("%2d: %d %d %d\n", i, pids[i].pid, pids[i].type, pids[i].subtype);
                }
            }
        }
        ServiceFree(service);
    }
    else
    {
        printf("Could not find \"%s\"\n", argument);
    }
}

static void CommandStats(char *argument)
{
    int i;
    printf("Packet Statistics\n"
           "-----------------\n");

    for (i = 0; i < PIDFilterIndex_Count; i ++)
    {
        printf("%s Filter :\n", PIDFilterNames[i]);
        printf("\tFiltered  : %d\n", pidfilters[i]->packetsfiltered);
        printf("\tProcessed : %d\n", pidfilters[i]->packetsprocessed);
        printf("\tOutput    : %d\n", pidfilters[i]->packetsoutput);
    }
    printf("\n");

    printf("Manual Output Statistics\n"
           "------------------------\n");
    for (i = 0; i < MAX_OUTPUTS; i ++)
    {
        if (!outputs[i].name)
        {
            continue;
        }
        printf("%s Filter :\n", outputs[i].name);
        printf("\tFiltered  : %d\n", outputs[i].filter->packetsfiltered);
        printf("\tOutput    : %d\n", outputs[i].filter->packetsoutput);
    }

    printf("Total packets processed: %d\n", tsfilter->totalpackets);
    printf("Approximate TS bitrate : %gMbs\n", ((double)tsfilter->bitrate / (1024.0 * 1024.0)));
}

static void CommandAddOutput(char *argument)
{
    int i;
    Output_t *output = NULL;
    char *name;
    char *destination;

    /* Work out the name */
    name = argument;
    destination = strchr(name, ' ');
    *destination = 0;
    /* Trim spaces from the start of the address */
    destination = Trim(destination + 1);

    printlog(LOG_DEBUGV,"Name = \"%s\" Destination = \"%s\"\n", name, destination);

    for (i = 0; i < MAX_OUTPUTS; i ++)
    {
        if (outputs[i].name && (strcmp(name, outputs[i].name) == 0))
        {
            printf("Output already exists!\n");
            return;
        }
        if ((output == NULL ) && (outputs[i].name == NULL))
        {
            output = &outputs[i];
        }
    }
    if (!output)
    {
        printf("No free output slots!\n");
        return;
    }

    output->filter = PIDFilterAllocate(tsfilter);
    if (!output->filter)
    {
        printf("Failed to allocate PID filter!\n");
        return;
    }
    output->pids.pidcount = 0;
    output->filter->filterpacket = PIDFilterSimpleFilter;
    output->filter->fparg = &output->pids;
    output->filter->outputpacket = UDPOutputPacketOutput;
    output->filter->oparg = UDPOutputCreate(destination);
    if (!output->filter->oparg)
    {
        printf("Failed to parse ip and udp port!\n");
        PIDFilterFree(output->filter);
        return;
    }
    output->filter->enabled = 1;
    output->name = strdup(argument);


}

static void CommandRmOutput(char *argument)
{
    Output_t *output = NULL;
    char *name = Trim(argument);

    output = FindOutput(name);
    if (output == NULL)
    {
        return;
    }
    output->filter->enabled = 0;
    PIDFilterFree(output->filter);
    free(output->name);
    memset(output, 0, sizeof(Output_t));
}

static void CommandOutputs(char *argument)
{
    int i;
    for (i = 0; i < MAX_OUTPUTS; i ++)
    {
        if (outputs[i].name)
        {
            printf("%10s : %s\n",outputs[i].name,
                   UDPOutputDestination((void*)outputs[i].filter->oparg));
        }
    }
}

static void CommandAddPID(char *argument)
{
    uint16_t pid;
    Output_t *output = ParseOutputPID(argument, &pid);
    if (output)
    {
        output->filter->enabled = 0;
        output->pids.pids[output->pids.pidcount] = pid;
        output->pids.pidcount ++;
        output->filter->enabled = 1;
    }
}

static void CommandRmPID(char *argument)
{
    uint16_t pid;
    Output_t *output = ParseOutputPID(argument, &pid);
    if (output)
    {
        int i;
        for ( i = 0; i < output->pids.pidcount; i ++)
        {
            if (output->pids.pids[i] == pid)
            {
                memcpy(&output->pids.pids[i], &output->pids.pids[i + 1],
                       (output->pids.pidcount - (i + 1)) * sizeof(uint16_t));
                output->pids.pidcount --;
                break;
            }
        }
    }
}

static void CommandOutputPIDs(char *argument)
{
    int i;
    Output_t *output = NULL;
    char *name;

    if (argument == NULL)
    {
        printf("Missing output\n");
        return;
    }

    name = Trim(argument);

    output = FindOutput(name);

    if (!output)
    {
        return;
    }

    printf("PIDs for \'%s\' (%d):\n", name, output->pids.pidcount);

    for (i = 0; i < output->pids.pidcount; i ++)
    {
        printf("0x%x\n", output->pids.pids[i]);
    }

}

static Output_t *ParseOutputPID(char *argument, uint16_t *pid)
{
    char *name;
    char *pidstr;
    char *formatstr;

    name = argument;
    pidstr = strchr(argument, ' ');
    if (pidstr == NULL)
    {
        printf("Missing PID!\n");
        return NULL;
    }
    *pidstr = 0;
    pidstr ++;

    if ((pidstr[0] == '0') && (tolower(pidstr[1])=='x'))
    {
        pidstr[1] = 'x';
        formatstr = "0x%hx";
    }
    else
    {
        formatstr = "%hd";
    }

    if (sscanf(pidstr, formatstr, pid) == 0)
    {
        printf("Failed to parse \"%s\"\n", pidstr);
        return NULL;
    }

    printf("%s 0x%x %d\n", name, *pid, *pid);
    return FindOutput(name);
}

static Output_t *FindOutput(char *name)
{
    Output_t *output = NULL;
    int i;
    for (i = 0; i < MAX_OUTPUTS; i ++)
    {
        if (outputs[i].name && (strcmp(outputs[i].name,name) == 0))
        {
            output = &outputs[i];
            break;
        }
    }

    if (output == NULL)
    {
        printf("Failed to find output \"%s\"\n", name);
    }
    return output;
}

static void CommandHelp(char *argument)
{
    int i;
    if (argument)
    {
        int commandFound = 0;
        for (i = 0; commands[i].command; i ++)
        {
            if (strcasecmp(commands[i].command,argument) == 0)
            {
                printf("%s\n", commands[i].longhelp);
                commandFound = 1;
                break;
            }
        }
        if (!commandFound)
        {
            printf("No help for unknown command \"%s\"\n", argument);
        }
    }
    else
    {
        for (i = 0; commands[i].command; i ++)
        {
            printf("%10s - %s\n", commands[i].command, commands[i].shorthelp);
        }
    }
}
