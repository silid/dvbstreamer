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
#define _GNU_SOURCE
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include <getopt.h>
#include <ctype.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "commands.h"
#include "multiplexes.h"
#include "services.h"
#include "dvb.h"
#include "ts.h"
#include "udpoutput.h"
#include "logging.h"
#include "cache.h"
#include "outputs.h"
#include "main.h"
#include "deliverymethod.h"

#define PROMPT "DVBStream>"

#define MAX_ARGS (10)

static void GetCommand(char **command, char **argument);
static char **AttemptComplete (const char *text, int start, int end);
static char *CompleteCommand(const char *text, int state);
static bool ProcessCommand(char *command, char *argument);
static char **Tokenise(char *arguments, int *argc);
static void ParseLine(char *line, char **command, char **argument);
static char *Trim(char *str);
static int CommandPrintfImpl(char *fmt, ...);

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
static void CommandAddSSF(int argc, char **argv);
static void CommandRemoveSSF(int argc, char **argv);
static void CommandSSFS(int argc, char **argv);
static void CommandSetSSF(int argc, char **argv);
static void CommandFEStatus(int argc, char **argv);
static void CommandHelp(int argc, char **argv);

static int ParsePID(char *argument);

int (*CommandPrintf)(char *fmt, ...);

static char *PIDFilterNames[] = {
                                    "PAT",
                                    "PMT",
                                    "SDT",
                                    "Service",
                                };

static Command_t commands[] = {
                                  {
                                      "quit",
                                      FALSE, 0, 0,
                                      "Exit the program",
                                      "Exit the program, can be used in the startup file to stop further processing.",
                                      CommandQuit
                                  },
                                  {
                                      "services",
                                      FALSE, 0, 0,
                                      "List all available services",
                                      "Lists all the services currently in the database. This list will be "
                                      "updated as updates to the PAT are detected.",
                                      CommandServices
                                  },
                                  {
                                      "multiplex",
                                      FALSE, 0, 0,
                                      "List all the services on the current multiplex",
                                      "List only the services on the same multiplex as the currently selected service.",
                                      CommandMultiplex,
                                  },
                                  {
                                      "select",
                                      FALSE, 1, 1,
                                      "Select a new service to stream",
                                      "select <service name>\n"
                                      "Sets <service name> as the current service, this may mean tuning to a different "
                                      "multiplex.",
                                      CommandSelect
                                  },
								  {
                                      "current",
                                      FALSE, 0, 0,
                                      "Print out the service currently being streamed.",
                                      "Shows the service that is currently being streamed to the default output.",
                                      CommandCurrent
                                  },
                                  {
                                      "pids",
                                      FALSE, 1, 1,
                                      "List the PIDs for a specified service",
                                      "pids <service name>\n"
                                      "List the PIDs for <service name>.",
                                      CommandPids
                                  },
                                  {
                                      "stats",
                                      FALSE, 0, 0,
                                      "Display the stats for the PAT,PMT and service PID filters",
                                      "Display the number of packets processed and the number of packets "
                                      "filtered by each filter.",
                                      CommandStats
                                  },
                                  {
                                      "addoutput",
                                      TRUE, 2, 2,
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
                                      TRUE, 1, 1,
                                      "Remove a destination for manually filtered PIDs.",
                                      "rmoutput <output name>\n"
                                      "Removes the destination and stops all filters associated with this output.",
                                      CommandRmOutput
                                  },
                                  {
                                      "lsoutputs",
                                      FALSE, 0, 0,
                                      "List current outputs",
                                      "List all active additonal output names and destinations.",
                                      CommandOutputs
                                  },
                                  {
                                      "addpid",
                                      TRUE, 2, 2,
                                      "Adds a PID to filter to an output",
                                      "addpid <output name> <pid>\n"
                                      "Adds a PID to the filter to be sent to the specified output.",
                                      CommandAddPID
                                  },
                                  {
                                      "rmpid",
                                      TRUE, 2, 2,
                                      "Removes a PID to filter from an output",
                                      "rmpid <output name> <pid>\n"
                                      "Removes the PID from the filter that is sending packets to the specified output.",
                                      CommandRmPID
                                  },
                                  {
                                      "lspids",
                                      TRUE, 1, 1,
                                      "List PIDs for output",
                                      "lspids <output name>\n"
                                      "List the PIDs being filtered for a specific output",
                                      CommandOutputPIDs
                                  },
                                  {
                                      "addsf",
                                      TRUE, 2, 2,
                                      "Add a service filter for secondary services",
                                      "addsf <output name> <ipaddress>:<udp port>\n"
                                      "Adds a new destination for sending a secondary service to.",
                                      CommandAddSSF
                                  },
                                  {
                                      "rmsf",
                                      TRUE, 1, 1,
                                      "Remove a service filter for secondary services",
                                      "rmsf <output name>\n"
                                      "Remove a destination for sending secondary services to.",
                                      CommandRemoveSSF
                                  },
                                  {
                                      "lssfs",
                                      FALSE,0,0,
                                      "List all secondary service filters",
                                      "List all secondary service filters their names, destinations and currently selected service.",
                                      CommandSSFS
                                  },
                                  {
                                      "setsf",
                                      FALSE, 1, 1,
                                      "Select a service to stream to a secondary service output",
                                      "setsf <output name> <service name>\n"
                                      "Stream the specified service to the secondary service output.",
                                      CommandSetSSF
                                  },
                                  {
                                      "festatus",
                                      0, 0, 0,
                                      "Displays the status of the tuner.",
                                      "Displays whether the front end is locked, the bit error rate and signal to noise"
                                      "ratio and the signal strength",
                                      CommandFEStatus
                                  },
                                  {
                                      "help",
                                      TRUE, 0, 1,
                                      "Display the list of commands or help on a specific command",
                                      "help <command>\n"
                                      "Displays help for the specified command.",
                                      CommandHelp
                                  },
                                  {NULL, FALSE, 0, 0, NULL,NULL}
                              };
static char *args[MAX_ARGS];
static bool quit = FALSE;

int CommandInit(void)
{

    rl_readline_name = "DVBStreamer";
    rl_attempted_completion_function = AttemptComplete;
    return 0;
}

void CommandDeInit(void)
{
    /* Nothing to do for now */
}

/**************** Command Loop/Startup file functions ************************/
void CommandLoop(void)
{
    char *command;
    char *argument;
    quit = FALSE;
    CommandPrintf = CommandPrintfImpl;

    /* Start Command loop */
    while(!quit && !ExitProgram)
    {
        GetCommand(&command, &argument);

        if (command)
        {
            if (!ProcessCommand(command, argument))
            {
                CommandPrintf("Unknown command \"%s\"\n", command);
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

    quit = FALSE;
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

bool CommandExecute(char *line)
{
    bool commandFound = FALSE;
    char *command;
    char *argument;
    ParseLine(line, &command, &argument);
    if (command && (strlen(command) > 0))
    {
        commandFound = ProcessCommand(command, argument);
        free(command);
        if (argument)
        {
            free(argument);
        }
    }
    return commandFound;
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


static bool ProcessCommand(char *command, char *argument)
{
    char **argv = NULL;
    int argc = 0;
    int i;
    bool commandFound = FALSE;

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
                CommandPrintf("Incorrect number of arguments see help for more information!\n\n%s\n\n",commands[i].longhelp);
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

            commandFound = TRUE;
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
/************************** Output Functions *********************************/
static int CommandPrintfImpl(char *fmt, ...)
{
    int result = 0;
    char *output;
    va_list valist;
    va_start(valist, fmt);
    result = vasprintf(&output, fmt, valist);
    fputs(output, stdout);
    va_end(valist);
    free(output);
    return result;
}

/************************** Command Functions ********************************/
static void CommandQuit(int argc, char **argv)
{
    quit = TRUE;
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
            CommandPrintf("%4x: %s\n", service->id, service->name);
            ServiceFree(service);
        }
    }
    while(service && !ExitProgram);
    ServiceEnumeratorDestroy(enumerator);
}

static void CommandMultiplex(int argc, char **argv)
{
    if (CurrentMultiplex == NULL)
    {
        CommandPrintf("No multiplex currently selected!\n");
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
                CommandPrintf("%4x: %s\n", service->id, service->name);
                ServiceFree(service);
            }
        }
        while(service && !ExitProgram);
        ServiceEnumeratorDestroy(enumerator);
    }
}

static void CommandSelect(int argc, char **argv)
{
    Service_t *service;

    service = SetCurrentService(argv[0]);
    if (service)
    {
        CommandPrintf("Name      = %s\n", service->name);
        CommandPrintf("ID        = %04x\n", service->id);
    }
    else
    {
        CommandPrintf("Could not find \"%s\"\n", argv[0]);
    }
}

static void CommandCurrent(int argc, char **argv)
{
	if ( CurrentService)
	{
		CommandPrintf("Current Service : \"%s\" (0x%04x) Multiplex: %f MHz\n",
			CurrentService->name, CurrentService->id, (double)CurrentMultiplex->freq / 1000000.0);
	}
	else
	{
		CommandPrintf("No current service\n");
	}
}

static void CommandPids(int argc, char **argv)
{
    Service_t *service;

    service = ServiceFindName(argv[0]);
    if (service)
    {
        bool cached = TRUE;
        int i;
        int count;
        PID_t *pids;
        pids = CachePIDsGet(service, &count);
        if (pids == NULL)
        {
            count = ServicePIDCount(service);
            cached = FALSE;
        }
        CommandPrintf("%d PIDs for \"%s\" %s\n", count, argv[0], cached ? "(Cached)":"");
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
                    CommandPrintf("No memory to retrieve PIDs\n");
                }
            }

            for (i = 0; i < count; i ++)
            {
                CommandPrintf("%2d: %d %d %d\n", i, pids[i].pid, pids[i].type, pids[i].subtype);
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
        CommandPrintf("Could not find \"%s\"\n", argv[0]);
    }
}

static void CommandStats(int argc, char **argv)
{
    int i;
    CommandPrintf("PSI/SI Processor Statistics\n"
                          "---------------------------\n");

    for (i = 0; i < PIDFilterIndex_Count; i ++)
    {
        CommandPrintf("\t%-15s : %d\n", PIDFilterNames[i], PIDFilters[i]->packetsprocessed);
    }
    CommandPrintf("\n");

    CommandPrintf("Service Filter Statistics\n"
                          "-------------------------\n");
    for (i = 0; i < MAX_OUTPUTS; i ++)
    {
        if ((!Outputs[i].name) || (Outputs[i].type != OutputType_Service))
        {
            continue;
        }
        CommandPrintf("\t%-15s : %d\n", Outputs[i].name, Outputs[i].filter->packetsoutput);
    }
    CommandPrintf("\n");

    CommandPrintf("Manual Output Statistics\n"
                          "------------------------\n");
    for (i = 0; i < MAX_OUTPUTS; i ++)
    {
        if ((!Outputs[i].name) || (Outputs[i].type != OutputType_Manual))
        {
            continue;
        }
        CommandPrintf("\t%-15s : %d\n", Outputs[i].name, Outputs[i].filter->packetsoutput);
    }
    CommandPrintf("\n");



    CommandPrintf("Total packets processed: %d\n", TSFilter->totalpackets);
    CommandPrintf("Approximate TS bitrate : %gMbs\n", ((double)TSFilter->bitrate / (1024.0 * 1024.0)));
}

static void CommandAddOutput(int argc, char **argv)
{
    Output_t *output = NULL;

    printlog(LOG_DEBUGV,"Name = \"%s\" Destination = \"%s\"\n", argv[0], argv[1]);

    output = OutputAllocate(argv[0], OutputType_Manual, argv[1]);
    if (!output)
    {
        CommandPrintf("Failed to add output, reason \"%s\"\n", OutputErrorStr);
    }
}

static void CommandRmOutput(int argc, char **argv)
{
    Output_t *output = NULL;

    if (strcmp(argv[0], PrimaryService) == 0)
    {
        CommandPrintf("Cannot remove the primary output!\n");
        return;
    }

    output = OutputFind(argv[0], OutputType_Manual);
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
        if (Outputs[i].name && (Outputs[i].type == OutputType_Manual))
        {
            CommandPrintf("%10s : %s\n",Outputs[i].name,
                   DeliveryMethodGetMRL(Outputs[i].filter));
        }
    }
}

static void CommandAddPID(int argc, char **argv)
{
    Output_t *output = OutputFind(argv[0], OutputType_Manual);
    if (output)
    {
        int i;
        i = ParsePID(argv[1]);
        if (i < 0)
        {
            return;
        }
        OutputAddPID(output, (uint16_t)i);
    }
}

static void CommandRmPID(int argc, char **argv)
{
    Output_t *output = OutputFind(argv[0], OutputType_Manual);
    if (output)
    {
        int i;
        i = ParsePID(argv[1]);
        if (i < 0)
        {
            return;
        }
        OutputRemovePID(output, (uint16_t)i);
    }
}

static void CommandOutputPIDs(int argc, char **argv)
{
    int i;
    Output_t *output = NULL;
    int pidcount;
    uint16_t *pids;
    char *name;

    name = Trim(argv[0]);

    output = OutputFind(name, OutputType_Manual);

    if (!output)
    {
        return;
    }
    OutputGetPIDs(output, &pidcount, &pids);

    CommandPrintf("PIDs for \'%s\' (%d):\n", name, pidcount);

    for (i = 0; i <pidcount; i ++)
    {
        CommandPrintf("0x%x\n", pids[i]);
    }

}

static void CommandAddSSF(int argc, char **argv)
{
    Output_t *output = NULL;

    printlog(LOG_DEBUGV,"Name = \"%s\" Destination = \"%s\"\n", argv[0], argv[1]);

    output = OutputAllocate(argv[0], OutputType_Service, argv[1]);
    if (!output)
    {
        CommandPrintf("Failed to add output, reason \"%s\"\n", OutputErrorStr);
    }
}

static void CommandRemoveSSF(int argc, char **argv)
{
    Output_t *output = NULL;
    Service_t *oldService;

    if (strcmp(argv[0], PrimaryService) == 0)
    {
        CommandPrintf("You cannot remove the primary service!\n");
        return;
    }

    output = OutputFind(argv[0], OutputType_Service);
    if (output == NULL)
    {
        return;
    }
    OutputGetService(output, &oldService);
    OutputFree(output);
    if (oldService)
    {
        ServiceFree(oldService);
    }

}

static void CommandSSFS(int argc, char **argv)
{
    int i;
    for (i = 0; i < MAX_OUTPUTS; i ++)
    {
        if (Outputs[i].name && (Outputs[i].type == OutputType_Service))
        {
            Service_t *service;
            OutputGetService(&Outputs[i], &service);
            CommandPrintf("%10s : %s (%s)\n",Outputs[i].name,
                   DeliveryMethodGetMRL(Outputs[i].filter),
                   service ? service->name:"<NONE>");
        }
    }
}

static void CommandSetSSF(int argc, char **argv)
{
    Output_t *output = NULL;
    char *outputName = argv[0];
    char *serviceName;
    Service_t *service, *oldService = NULL;

    serviceName = strchr(outputName, ' ');
    if (!serviceName)
    {
        CommandPrintf("No service specified!");
        return;
    }

    serviceName[0] = 0;

    for (serviceName ++;*serviceName && isspace(*serviceName); serviceName ++);

    if (strcmp(outputName, PrimaryService) == 0)
    {
        CommandPrintf("Use \'select\' to change the primary service!\n");
        return;
    }

    output = OutputFind(outputName, OutputType_Service);
    if (output == NULL)
    {
        CommandPrintf("Failed to find output %s\n", outputName);
        return;
    }

    service = ServiceFindName(serviceName);
    if (service == NULL)
    {
        CommandPrintf("Failed to find service %s\n", serviceName);
        return;
    }
    OutputGetService(output, &oldService);

    if (OutputSetService(output, service))
    {
        CommandPrintf("Failed to set service, reason %s\n", OutputErrorStr);
    }

    if (oldService)
    {
        ServiceFree(oldService);
    }
}

static void CommandFEStatus(int argc, char **argv)
{
    fe_status_t status;
    unsigned int ber, strength, snr;
    DVBFrontEndStatus(DVBAdapter, &status, &ber, &strength, &snr);

    CommandPrintf("Tuner status:  %s%s%s%s%s%s\n",
             (status & FE_HAS_SIGNAL)?"Signal, ":"",
             (status & FE_TIMEDOUT)?"Timed out, ":"",
             (status & FE_HAS_LOCK)?"Lock, ":"",
             (status & FE_HAS_CARRIER)?"Carrier, ":"",
             (status & FE_HAS_VITERBI)?"VITERBI, ":"",
             (status & FE_HAS_SYNC)?"Sync, ":"");
    CommandPrintf("BER = %d Signal Strength = %d SNR = %d\n", ber, strength, snr);
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
                CommandPrintf("%s\n\n", commands[i].longhelp);
                commandFound = 1;
                break;
            }
        }
        if (!commandFound)
        {
            CommandPrintf("No help for unknown command \"%s\"\n", argv[0]);
        }
    }
    else
    {
        for (i = 0; commands[i].command; i ++)
        {
            CommandPrintf("%10s - %s\n", commands[i].command, commands[i].shorthelp);
        }
    }
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
        CommandPrintf("Failed to parse \"%s\"\n", argument);
        return -1;
    }

    return pid;
}
