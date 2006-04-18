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
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <getopt.h>
#include <stdarg.h>
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
#include "cache.h"

#define PROMPT "DVBStream>"

enum PIDFilterIndex
{
	PIDFilterIndex_PAT = 0,
	PIDFilterIndex_PMT,
	PIDFilterIndex_Service,
	
	PIDFilterIndex_Count
};

typedef struct Command_t
{
	char *command;
	char *help;
	void (*commandfunc)(char *argument);
}Command_t;

static void usage();
static int ServiceFilterPacket(void *arg, TSPacket_t *packet);
static PIDFilter_t *SetupPIDFilter(TSFilter_t *tsfilter,
								   PacketProcessor filterpacket,  void *fparg,
								   PacketProcessor processpacket, void *pparg,
								   PacketProcessor outputpacket,  void *oparg);
static void GetCommand(char **command, char **argument);

static void CommandQuit(char *argument);
static void CommandServices(char *argument);
static void CommandMultiplex(char *argument);
static void CommandSelect(char *argument);
static void CommandPids(char *argument);
static void CommandStats(char *argument);
static void CommandHelp(char *argument);

volatile Multiplex_t *CurrentMultiplex = NULL;
volatile Service_t *CurrentService = NULL;

int verbosity = 0;


static int quit = 0;
static DVBAdapter_t *adapter;
static TSFilter_t *tsfilter;
static PIDFilter_t *pidfilters[3];

static Command_t commands[] = {
	{
		"quit",
		"Exit the program",
		CommandQuit
	},
	{
		"services",
		"List all available services",
		CommandServices
	},
	{
		"multiplex",
		"List all the services on the current multiplex",
		CommandMultiplex,
	},
	{
		"select",
		"Select a new service to stream",
		CommandSelect
	},
	{
		"pids",
		"List the PIDs for a specified service",
		CommandPids
	},
	{
		"stats",
		"Display the stats for the PAT,PMT and service PID filters",
		CommandStats
	},
    {
        "help",
        "Display the list of commands",
        CommandHelp
    },
	{NULL,NULL,NULL}
};


int main(int argc, char *argv[])
{
    char channelsFile[PATH_MAX];
	void *outputArg = NULL;
	int i;
	int adapterNumber = 0;
	PIDFilterSimpleFilter_t patsimplefilter;
	void *patprocessor;
	void *pmtprocessor;
	
	channelsFile[0] = 0;

	while (1) 
	{
        char c;
        c = getopt(argc, argv, "vo:a:t:");
        if (c == -1) {
            break;
        }
        switch (c)
        {
            case 'v': verbosity ++;
                      break;
                      break;
			case 'o': 
					  outputArg = UDPOutputCreate(optarg);
					  if (outputArg == NULL)
					  {
					      printlog(0, "Error creating UDP output!\n");
						  exit(1);
					  }
					  printlog(1,"Output will be via UDP to %s\n", optarg);
					  break;
            case 'a': adapterNumber = atoi(optarg);
                      printlog(1,"Using adapter %d\n", adapterNumber);
                      break;
            case 't': strcpy(channelsFile, optarg);
                      printlog(1,"Using channels file %s\n", channelsFile);
                      break;
            default:
                usage();
                exit(1);
        }
    }
	if (outputArg == NULL)
	{
		printlog(0, "No output set!\n");
        usage();
		exit(1);
	}

	if (DBaseInit(adapterNumber))
	{
		printlog(1, "Failed to initialise database\n");
		exit(1);
	}
	
	if (CacheInit())
	{
		printlog(1,"Failed to initialise cache\n");
		exit(1);
	}

	if (strlen(channelsFile))
	{
		printlog(1,"Importing services from %s\n", channelsFile);
		if (!parsezapfile(channelsFile))
		{
		  exit(1);
		}
    }
	printlog(1, "%d Services available on %d Multiplexes\n", ServiceCount(), MultiplexCount());
	/* Initialise the DVB adapter */
	adapter = DVBInit(adapterNumber);
	if (!adapter)
	{
		printlog(0, "Failed to open DVB adapter!\n");
		exit(1);
	}

	DVBDemuxStreamEntireTSToDVR(adapter);

	/* Create Transport stream filter thread */
	tsfilter = TSFilterCreate(adapter);
	if (!tsfilter)
	{
		printlog(0, "Failed to create filter!\n");
		exit(1);
	}

	/* Create PAT filter */
	patsimplefilter.pidcount = 1;
	patsimplefilter.pids[0] = 0;
	patprocessor = PATProcessorCreate();
	pidfilters[PIDFilterIndex_PAT] = SetupPIDFilter(tsfilter, 
		PIDFilterSimpleFilter, &patsimplefilter,
		PATProcessorProcessPacket, patprocessor,
		(PacketProcessor)UDPOutputPacketOutput,outputArg);
	
	/* Create PMT filter */	
	pmtprocessor = PMTProcessorCreate();	
	pidfilters[PIDFilterIndex_PMT] = SetupPIDFilter(tsfilter, 
		PMTProcessorFilterPacket, NULL,
		PMTProcessorProcessPacket, pmtprocessor,
		(PacketProcessor)UDPOutputPacketOutput,outputArg);

	/* Create Service filter */
	pidfilters[PIDFilterIndex_Service] = SetupPIDFilter(tsfilter, 
		ServiceFilterPacket, NULL,
		NULL, NULL,
		(PacketProcessor)UDPOutputPacketOutput,outputArg);
	
	for (i = 0; i < PIDFilterIndex_Count; i ++)
	{
		pidfilters[i]->enabled = 1;
	}
	
	/* Start Command loop */
	while(!quit)
	{
		char *command, *argument;
		GetCommand(&command, &argument);
		if (command)
		{
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
			if (!commandFound)
			{
				printf("Unknown command \"%s\"\n", command);
			}
		}

	}
	
	DVBDispose(adapter);
	TSFilterDestroy(tsfilter);
	
	// Close output
	UDPOutputClose(outputArg);
	CacheDeInit();
	DBaseDeInit();
    return 0;
}

// Print out a log message to stderr depending on verbosity
void printlog(int level, char *format, ...)
{
    if (level <= verbosity)
    {
        va_list valist;
        char *logline;
        va_start(valist, format);
        vasprintf(&logline, format, valist);
        fprintf(stderr, logline);
        va_end(valist);
        free(logline);
    }
}
int ServiceFilterCount = 0;
int ServiceFilterPacket(void *arg, TSPacket_t *packet)
{
	unsigned short pid = TSPACKET_GETPID(*packet);
	int i;
	if (CurrentService)
	{
		int count;
		PID_t *pids;
		ServiceFilterCount ++;
		pids = CachePIDsGet(CurrentService, &count);
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

// Find the service named <name> and tune to the new frequency for the multiplex the service is
// on (if required) and then select the new service id to filter packets for.
Service_t *SetCurrentService(DVBAdapter_t *adapter, TSFilter_t *tsfilter, char *name)
{
	Multiplex_t *multiplex;
	Service_t *service = CacheServiceFindName(name, &multiplex);
	
	printlog(1,"Writing changes back to database.\n");
	CacheWriteback();
	
	if (!service)
	{
		return NULL;
	}
	
	printlog(1, "Service found id:0x%04x Multiplex:%d\n", service->id, service->multiplexfreq);
	if ((CurrentService == NULL) || (!ServiceAreEqual(service,CurrentService)))
	{
		printlog(1,"Disabling filters\n");
		TSFilterEnable(tsfilter, 0);
		
		if ((CurrentMultiplex!= NULL) && MultiplexAreEqual(multiplex, CurrentMultiplex))
		{
			printlog(1,"Same multiplex\n");
			CurrentService = service;
		}
		else
		{
			struct dvb_frontend_parameters feparams;
			if (CurrentMultiplex)
			{
				free(CurrentMultiplex);
			}
						
			printlog(1,"Caching Services\n");
			CacheLoad(multiplex);
			CurrentMultiplex = multiplex;
			
			printlog(1,"Getting Frondend parameters\n");
			MultiplexFrontendParametersGet(CurrentMultiplex, &feparams);
			
			printlog(1,"Tuning\n");
			DVBFrontEndTune(adapter, &feparams);

			CurrentService = CacheServiceFindId(service->id);
			ServiceFree(service);
			{
				int i;
				int count;
				PID_t *pids = CachePIDsGet(CurrentService, &count);
				printf("PID count = %d\n", count);
				for (i = 0; i < count; i ++)
				{
					printf("%2d: %d\n", i, pids[i].pid, pids[i].type, pids[i].subtype);
				}
			}
		}
		printlog(1,"Enabling filters\n");
		TSFilterEnable(tsfilter, 1);
	}
	
	return CurrentService;
}
/******************** Command Functions ********************/
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
	}while(service);
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
		}while(service);
	}
}

static void CommandSelect(char *argument)
{
	Service_t *service = SetCurrentService(adapter, tsfilter, argument);
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
	Service_t *service = ServiceFindName(argument);
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
					printf("%2d: %d\n", i, pids[i].pid, pids[i].type, pids[i].subtype);
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
	printf("PAT packets filtered     : %d\n", pidfilters[PIDFilterIndex_PAT]->packetsprocessed);
	printf("PMT packets filtered     : %d\n", pidfilters[PIDFilterIndex_PMT]->packetsprocessed);
	printf("Service packets filtered : %d\n", pidfilters[PIDFilterIndex_Service]->packetsprocessed);
	printf("Service Filter Count     : %d\n", ServiceFilterCount);
}

static void CommandHelp(char *argument)
{
    int i;
    for (i = 0; commands[i].command; i ++)
    {
        printf("%10s - %s\n", commands[i].command, commands[i].help);
    }
}
/****************** Static functions **************************/
/*
 * Output command line usage and help.
 */
static void usage()
{
    fprintf(stderr,"Usage:dvbstream <options>\n"
                   "      Options:\n"
                   "      -v            : Increase the amount of debug output,\n"
                   "                      can be used multiple times for more output\n"
				   "      -o <host:port>: Output transport stream via UDP to the given host and port\n"
				   "                      (Only one method of output can be selected)\n"
                   "      -a <adapter>  : Use adapter number\n"
                   "      -t <file>     : channels.conf file to import services and multiplexes from\n"
           );
}

static PIDFilter_t *SetupPIDFilter(TSFilter_t *tsfilter,
								   PacketProcessor filterpacket,  void *fparg,
								   PacketProcessor processpacket, void *pparg,
								   PacketProcessor outputpacket,  void *oparg)
{
	PIDFilter_t *filter;
	filter = PIDFilterAllocate(tsfilter);

	filter->filterpacket = filterpacket;
	filter->fparg = fparg;
	
	filter->processpacket = processpacket;
	filter->pparg = pparg;
	
	filter->outputpacket = outputpacket;
	filter->oparg = oparg;
}

static void GetCommand(char **command, char **argument)
{
	char *line = readline(PROMPT);
	*command = NULL;
	*argument = NULL;

	/* If the user has entered a non blank line process it */
	if (line && line[0])
	{
		char *space = strchr(line, ' ');

		add_history(line);

		if (space)
		{
			*space = 0;
			*argument = malloc(strlen(space + 1) + 1);
			if (*argument)
			{
				strcpy(*argument, space + 1);
			}
		}
		else
		{
			*argument = "";
		}
		*command = malloc(strlen(line) + 1);
		if (*command)
		{
			strcpy(*command, line);
		}
	}
	
	/* Make sure we free the line buffer */
	if (line)
	{
		free(line);
	}
}
