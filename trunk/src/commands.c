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
 
commands.c
 
Command Processing and command functions.
 
*/
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <getopt.h>
#include <ctype.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "multiplexes.h"
#include "services.h"
#include "dvb.h"
#include "ts.h"
#include "udpoutput.h"
#include "logging.h"
#include "cache.h"
#include "main.h"

#define PROMPT "DVBStream>"

#define MAX_ARGS (10)
#define MAX_OUTPUTS (MAX_FILTERS - PIDFilterIndex_Count)

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
    int   tokenise;
    int   minargs;
    int   maxargs;
    char *shorthelp;
    char *longhelp;
    void (*commandfunc)(int argc, char **argv);
}
Command_t;

static void GetCommand(char **command, char **argument);
static char **AttemptComplete (const char *text, int start, int end);
static char *CompleteCommand(const char *text, int state);
static int ProcessCommand(char *command, char *argument);
static char **Tokenise(char *arguments, int *argc);
static void ParseLine(char *line, char **command, char **argument);
static char *Trim(char *str);

static void CommandQuit(int argc, char **argv);
static void CommandServices(int argc, char **argv);
static void CommandMultiplex(int argc, char **argv);
static void CommandSelect(int argc, char **argv);
static void CommandCurrent(int argc, char **argv);
static void CommandPids(int argc, char **argv);
static void CommandStats(int argc, char **argv);
static void CommandAddOutput(int argc, char **argv);
static void CommandRmOutput(int argc, char **argv);
static void CommandOutputs(int argc, char **argv);
static void CommandAddPID(int argc, char **argv);
static void CommandRmPID(int argc, char **argv);
static void CommandOutputPIDs(int argc, char **argv);
static void CommandHelp(int argc, char **argv);

static int ParsePID(char *argument);
static Output_t *OutputAllocate(char *name, char *destination);
static void OutputFree(Output_t *output);
static Output_t *OutputFind(char *name);


static char *PIDFilterNames[] = {
                                    "PAT",
                                    "PMT",
                                    "SDT",
                                    "Service",
                                };

static Command_t commands[] = {
                                  {
                                      "quit",
                                      0, 0, 0,
                                      "Exit the program",
                                      "Exit the program, can be used in the startup file to stop further processing.",
                                      CommandQuit
                                  },
                                  {
                                      "services",
                                      0, 0, 0,
                                      "List all available services",
                                      "Lists all the services currently in the database. This list will be "
                                      "updated as updates to the PAT are detected.",
                                      CommandServices
                                  },
                                  {
                                      "multiplex",
                                      0, 0, 0,
                                      "List all the services on the current multiplex",
                                      "List only the services on the same multiplex as the currently selected service.",
                                      CommandMultiplex,
                                  },
                                  {
                                      "select",
                                      0, 1, 1,
                                      "Select a new service to stream",
                                      "select <service name>\n"
                                      "Sets <service name> as the current service, this may mean tuning to a different "
                                      "multiplex.",
                                      CommandSelect
                                  },
								  {
                                      "current",
                                      0, 0, 0,
                                      "Print out the service currently being streamed.",
                                      "Shows the service that is currently being streamed to the default output.",
                                      CommandCurrent
                                  },
                                  {
                                      "pids",
                                      0, 1, 1,
                                      "List the PIDs for a specified service",
                                      "pids <service name>\n"
                                      "List the PIDs for <service name>.",
                                      CommandPids
                                  },
                                  {
                                      "stats",
                                      0, 0, 0,
                                      "Display the stats for the PAT,PMT and service PID filters",
                                      "Display the number of packets processed and the number of packets "
                                      "filtered by each filter.",
                                      CommandStats
                                  },
                                  {
                                      "addoutput",
                                      1, 2, 2,
                                      "Add a new destination for manually filtered PIDs.",
                                      "addoutput <output name> <ipaddress>:<udp port>\n"
                                      "Adds a new destination for sending packets to. This is only used for "
                                      "manually filtered packets. "
                                      "To send packets to this destination you'll need to also call \'filterpid\' "
                                      "with this output as an argument.",
                                      CommandAddOutput
                                  },
                                  {
                                      "rmoutput",
                                      1, 1, 1,
                                      "Remove a destination for manually filtered PIDs.",
                                      "rmoutput <output name>\n"
                                      "Removes the destination and stops all filters associated with this output.",
                                      CommandRmOutput
                                  },
                                  {
                                      "outputs",
                                      0, 0, 0,
                                      "List current outputs",
                                      "List all active additonal output names and destinations.",
                                      CommandOutputs
                                  },
                                  {
                                      "addpid",
                                      1, 2, 2,
                                      "Adds a PID to filter to an output",
                                      "addpid <output name> <pid>\n"
                                      "Adds a PID to the filter to be sent to the specified output.",
                                      CommandAddPID
                                  },
                                  {
                                      "rmpid",
                                      1, 2, 2,
                                      "Removes a PID to filter from an output",
                                      "rmpid <output name> <pid>\n"
                                      "Removes the PID from the filter that is sending packets to the specified output.",
                                      CommandRmPID
                                  },
                                  {
                                      "outputpids",
                                      1, 1, 1,
                                      "List PIDs for output",
                                      "outputpids <output name>\n"
                                      "List the PIDs being filtered for a specific output",
                                      CommandOutputPIDs
                                  },
                                  {
                                      "help",
                                      1, 0, 1,
                                      "Display the list of commands or help on a specific command",
                                      "help <command>\n"
                                      "Displays help for the specified command.",
                                      CommandHelp
                                  },
                                  {NULL, 0, 0, 0, NULL,NULL}
                              };

static Output_t outputs[MAX_OUTPUTS];
static char *args[MAX_ARGS];
static int quit = 0;

int CommandInit(void)
{
    /* Clear all outputs */
    memset(&outputs, 0, sizeof(outputs));
    
    rl_readline_name = "DVBStreamer";
    rl_attempted_completion_function = AttemptComplete;
    return 0;
}

void CommandDeInit(void)
{
    int i;
    for (i = 0; i < MAX_OUTPUTS; i ++)
    {
        if (!outputs[i].name)
        {
            continue;
        }
        OutputFree(&outputs[i]);
    }
}

/**************** Command Loop/Startup file functions ************************/
void CommandLoop(void)
{
    char *command;
    char *argument;
    quit = 0;
    /* Start Command loop */
    while(!quit && !ExitProgram)
    {
        GetCommand(&command, &argument);

        if (command)
        {
            if (!ProcessCommand(command, argument))
            {
                fprintf(rl_outstream, "Unknown command \"%s\"\n", command);
            }
            free(command);
            if (argument)
            {
                free(argument);
            }
        }
    }
}

int CommandProcessFile(char *file)
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
                fprintf(stderr, "%s(%d): Unknown command \"%s\"\n", file, lineno, command);
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

static char **AttemptComplete (const char *text, int start, int end)
{
    char **matches = (char **)NULL;

    if (start == 0)
    {
        matches = rl_completion_matches (text, CompleteCommand);
    }
    else 
    {
        rl_attempted_completion_over = 1;
    }
    return (matches);
}

static char *CompleteCommand(const char *text, int state)
{
    static int lastIndex = -1, textlen;
    int i;
    
    if (state == 0)
    {
        lastIndex = -1;
        textlen = strlen(text);
    }
    
    for ( i = lastIndex + 1; commands[i].command; i ++)
    {
        if (strncasecmp(text, commands[i].command, textlen) == 0)
        {
            lastIndex = i;
            return strdup(commands[i].command);
        }
    }
    return NULL;
}


static int ProcessCommand(char *command, char *argument)
{
    char **argv = NULL;
    int argc = 0;
    int i;
    int commandFound = 0;
    
    for (i = 0; commands[i].command; i ++)
    {
        if (strcasecmp(command,commands[i].command) == 0)
        {
            
            if (argument)
            {
                if (commands[i].tokenise)
                {
                    argv = Tokenise(argument, &argc);
                }
                else
                {
                    argc = 1;
                    argv = args;
                    args[0] = argument;
                }
            }
            else
            {
                argc = 0;
                argv = args;
                args[0] = NULL;
            }
            
            if ((argc >= commands[i].minargs) && (argc <= commands[i].maxargs))
            {
                commands[i].commandfunc(argc, argv );
            }
            else
            {
                fprintf(rl_outstream, "Incorrect number of arguments see help for more information!\n\n%s\n\n",commands[i].longhelp);
            }
            
            if (commands[i].tokenise)
            {
                int a;

                /* Free the arguments but not the array as that is a static array */
                for (a = 0; a < argc; a ++)
                {
                    free(argv[a]);
                }
            }
           
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
        char *tmp;
        *space = 0;
        tmp = Trim(space + 1);
        if (strlen(tmp) > 0)
        {
            *argument = strdup(tmp);
        }
    }
    *command = strdup(line);
}

static char **Tokenise(char *arguments, int *argc)
{
    int currentarg = 0;
    char *start = arguments;
    char *end;
    char t;
    
    while (*start)
    {
        /* Trim spaces from the start */
        for (; *start && isspace(*start); start ++);
        
        /* Work out the end of the argument (Very simplistic for the moment no quotes allowed) */
        for (end = start; *end && !isspace(*end); end ++);
        t = end[0];
        end[0] = 0;
        args[currentarg] = strdup(start);
        end[0] = t;
        start = end;
        currentarg ++;
        
        if (currentarg >= MAX_ARGS)
        {
            int i;
            for ( i = 0; i < MAX_ARGS; i ++)
            {
                free(args[i]);
            }
            *argc = 0;
            return NULL;
        }
    }
    *argc = currentarg;
    return args;
}

static char *Trim(char *str)
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
static void CommandQuit(int argc, char **argv)
{
    quit = 1;
}

static void CommandServices(int argc, char **argv)
{
    ServiceEnumerator_t enumerator = ServiceEnumeratorGet();
    Service_t *service;
    do
    {
        service = ServiceGetNext(enumerator);
        if (service)
        {
            fprintf(rl_outstream, "%4x: %s\n", service->id, service->name);
            ServiceFree(service);
        }
    }
    while(service && !ExitProgram);
}

static void CommandMultiplex(int argc, char **argv)
{
    if (CurrentMultiplex == NULL)
    {
        fprintf(rl_outstream, "No multiplex currently selected!\n");
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
                fprintf(rl_outstream, "%4x: %s\n", service->id, service->name);
                ServiceFree(service);
            }
        }
        while(service && !ExitProgram);
    }
}

static void CommandSelect(int argc, char **argv)
{
    Service_t *service;

    service = SetCurrentService(argv[0]);
    if (service)
    {
        fprintf(rl_outstream, "Name      = %s\n", service->name);
        fprintf(rl_outstream, "ID        = %04x\n", service->id);
    }
    else
    {
        fprintf(rl_outstream, "Could not find \"%s\"\n", argv[0]);
    }
}

static void CommandCurrent(int argc, char **argv)
{
	if ( CurrentService)
	{
		fprintf(rl_outstream, "Current Service : \"%s\" (0x%04x) Multiplex: %f MHz\n", 
			CurrentService->name, CurrentService->id, (double)CurrentMultiplex->freq / 1000000.0);
	}
	else
	{
		fprintf(rl_outstream, "No current service\n");
	}
}

static void CommandPids(int argc, char **argv)
{
    Service_t *service;

    service = ServiceFindName(argv[0]);
    if (service)
    {
        int cached = 1;
        int i;
        int count;
        PID_t *pids;
        pids = CachePIDsGet(service, &count);
        if (pids == NULL)
        {
            count = ServicePIDCount(service);
            cached = 0;
        }
        fprintf(rl_outstream, "%d PIDs for \"%s\" %s\n", count, argv[0], cached ? "(Cached)":"");
        if (count > 0)
        {
            if (!cached)
            {
                pids = calloc(count, sizeof(PID_t));
                if (pids)
                {
                    ServicePIDGet(service, pids, &count);
                }
                else
                {
                    fprintf(rl_outstream, "No memory to retrieve PIDs\n");
                }
            }
            
            for (i = 0; i < count; i ++)
            {
                fprintf(rl_outstream, "%2d: %d %d %d\n", i, pids[i].pid, pids[i].type, pids[i].subtype);
            }
            
            if (!cached)
            {
                free(pids);
            }
        }
        ServiceFree(service);
    }
    else
    {
        fprintf(rl_outstream, "Could not find \"%s\"\n", argv[0]);
    }
}

static void CommandStats(int argc, char **argv)
{
    int i;
    fprintf(rl_outstream, "Packet Statistics\n"
           "-----------------\n");

    for (i = 0; i < PIDFilterIndex_Count; i ++)
    {
        fprintf(rl_outstream, "%s Filter :\n", PIDFilterNames[i]);
        fprintf(rl_outstream, "\tFiltered  : %d\n", PIDFilters[i]->packetsfiltered);
        fprintf(rl_outstream, "\tProcessed : %d\n", PIDFilters[i]->packetsprocessed);
        fprintf(rl_outstream, "\tOutput    : %d\n", PIDFilters[i]->packetsoutput);
    }
    fprintf(rl_outstream, "\n");

    fprintf(rl_outstream, "Manual Output Statistics\n"
           "------------------------\n");
    for (i = 0; i < MAX_OUTPUTS; i ++)
    {
        if (!outputs[i].name)
        {
            continue;
        }
        fprintf(rl_outstream, "%s Filter :\n", outputs[i].name);
        fprintf(rl_outstream, "\tFiltered  : %d\n", outputs[i].filter->packetsfiltered);
        fprintf(rl_outstream, "\tOutput    : %d\n", outputs[i].filter->packetsoutput);
    }

    fprintf(rl_outstream, "Total packets processed: %d\n", TSFilter->totalpackets);
    fprintf(rl_outstream, "Approximate TS bitrate : %gMbs\n", ((double)TSFilter->bitrate / (1024.0 * 1024.0)));
}

static void CommandAddOutput(int argc, char **argv)
{
    Output_t *output = NULL;

    printlog(LOG_DEBUGV,"Name = \"%s\" Destination = \"%s\"\n", argv[0], argv[1]);
    
    output = OutputAllocate(argv[0], argv[1]);
}

static void CommandRmOutput(int argc, char **argv)
{
    Output_t *output = NULL;

    output = OutputFind(argv[0]);
    if (output == NULL)
    {
        return;
    }
    OutputFree(output);
}

static void CommandOutputs(int argc, char **argv)
{
    int i;
    for (i = 0; i < MAX_OUTPUTS; i ++)
    {
        if (outputs[i].name)
        {
            fprintf(rl_outstream, "%10s : %s\n",outputs[i].name,
                   UDPOutputDestination((void*)outputs[i].filter->oparg));
        }
    }
}

static void CommandAddPID(int argc, char **argv)
{
    uint16_t pid;
    Output_t *output = OutputFind(argv[0]);    
    if (output)
    {
        int i;
        i = ParsePID(argv[1]);
        if (1 < 0)
        {
            return;
        }
        pid = (uint16_t)i;
        
        output->filter->enabled = 0;
        output->pids.pids[output->pids.pidcount] = pid;
        output->pids.pidcount ++;
        output->filter->enabled = 1;
    }
}

static void CommandRmPID(int argc, char **argv)
{
    uint16_t pid;
    Output_t *output = OutputFind(argv[0]);    
    if (output)
    {
        int i;
        i = ParsePID(argv[1]);
        if (1 < 0)
        {
            return;
        }
        pid = (uint16_t)i;
        
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

static void CommandOutputPIDs(int argc, char **argv)
{
    int i;
    Output_t *output = NULL;
    char *name;

    name = Trim(argv[0]);

    output = OutputFind(name);

    if (!output)
    {
        return;
    }

    fprintf(rl_outstream, "PIDs for \'%s\' (%d):\n", name, output->pids.pidcount);

    for (i = 0; i < output->pids.pidcount; i ++)
    {
        fprintf(rl_outstream, "0x%x\n", output->pids.pids[i]);
    }

}

static void CommandHelp(int argc, char **argv)
{
    int i;
    if (argc)
    {
        int commandFound = 0;
        for (i = 0; commands[i].command; i ++)
        {
            if (strcasecmp(commands[i].command,argv[0]) == 0)
            {
                fprintf(rl_outstream, "%s\n\n", commands[i].longhelp);
                commandFound = 1;
                break;
            }
        }
        if (!commandFound)
        {
            fprintf(rl_outstream, "No help for unknown command \"%s\"\n", argv[0]);
        }
    }
    else
    {
        for (i = 0; commands[i].command; i ++)
        {
            fprintf(rl_outstream, "%10s - %s\n", commands[i].command, commands[i].shorthelp);
        }
    }
}
/* Output Management functions */
static Output_t *OutputAllocate(char *name, char *destination)
{
    Output_t *output = NULL;
    int i;
    
    for (i = 0; i < MAX_OUTPUTS; i ++)
    {
        if (outputs[i].name && (strcmp(name, outputs[i].name) == 0))
        {
            fprintf(rl_outstream, "Output already exists!\n");
            return NULL;
        }
        if ((output == NULL ) && (outputs[i].name == NULL))
        {
            output = &outputs[i];
        }
    }
    if (!output)
    {
        fprintf(rl_outstream, "No free output slots!\n");
        return NULL;
    }

    output->filter = PIDFilterAllocate(TSFilter);
    if (!output->filter)
    {
        fprintf(rl_outstream, "Failed to allocate PID filter!\n");
        return NULL;
    }
    output->pids.pidcount = 0;
    output->filter->filterpacket = PIDFilterSimpleFilter;
    output->filter->fparg = &output->pids;
    output->filter->outputpacket = UDPOutputPacketOutput;
    output->filter->oparg = UDPOutputCreate(destination);
    if (!output->filter->oparg)
    {
        fprintf(rl_outstream, "Failed to parse ip and udp port!\n");
        PIDFilterFree(output->filter);
        return NULL;
    }
    output->filter->enabled = 1;
    output->name = strdup(name);

    return output;
}

static void OutputFree(Output_t *output)
{
    output->filter->enabled = 0;
    PIDFilterFree(output->filter);
    free(output->name);
    UDPOutputClose(output->filter->oparg);
    memset(output, 0, sizeof(Output_t));
}

static Output_t *OutputFind(char *name)
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
        fprintf(rl_outstream, "Failed to find output \"%s\"\n", name);
    }
    return output;
}

static int ParsePID(char *argument)
{
    char *formatstr;
    int pid;

    if ((argument[0] == '0') && (tolower(argument[1])=='x'))
    {
        argument[1] = 'x';
        formatstr = "0x%hx";
    }
    else
    {
        formatstr = "%hd";
    }

    if (sscanf(argument, formatstr, &pid) == 0)
    {
        fprintf(rl_outstream, "Failed to parse \"%s\"\n", argument);
        return -1;
    }

    return pid;
}
