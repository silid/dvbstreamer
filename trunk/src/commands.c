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
#include "patprocessor.h"
#include "pmtprocessor.h"
#include "sdtprocessor.h"
#include "psipprocessor.h"

/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/
#define PROMPT "DVBStreamer>"

#define MAX_ARGS (10)

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
/* External Command Prototypes. */
void CommandInstallPids(void);
void CommandUnInstallPids(void);

void CommandInstallInfo(void);
void CommandUnInstallInfo(void);

void CommandInstallServiceFilter(void);
void CommandUnInstallServiceFilter(void);

extern void CommandInstallScanning(void);
extern void CommandUnInstallScanning(void);

/* File Prototypes */    
static char **AttemptComplete (const char *text, int start, int end);
static char *CompleteCommand(const char *text, int state);
static bool ProcessCommand(char *command, char *argument);
static Command_t *FindCommand(Command_t *commands, char *command);
static char **Tokenise(char *arguments, int *argc);
static void ParseLine(char *line, char **command, char **argument);


static void CommandQuit(int argc, char **argv);
static void CommandSelect(int argc, char **argv);
static void CommandHelp(int argc, char **argv);
static void CommandGet(int argc, char **argv);
static void CommandSet(int argc, char **argv);
static void CommandVars(int argc, char **argv);


/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/

int (*CommandPrintf)(const char *fmt, ...);
CommandContext_t *CurrentCommandContext;
static CommandContext_t ConsoleCommandContext = {"console", FALSE, NULL, NULL, TRUE, 0, {0}};

static Command_t coreCommands[] = 
{
    {
        "quit",
        FALSE, 0, 0,
        "Exit the program.",
        "Exit the program, can be used in the startup file to stop further processing.",
        CommandQuit
    },
    {
        "select",
        FALSE, 1, 1,
        "Select a new service to stream.",
        "select <service name>\n"
        "Sets <service name> as the current service, this may mean tuning to a different "
        "multiplex.",
        CommandSelect
    },
    {
        "help",
        TRUE, 0, 1,
        "Display the list of commands or help on a specific command.",
        "help [<command>]\n"
        "List all available commands or displays specific help for the command specifed.",
        CommandHelp
    },
    {
        "get",
        TRUE, 1, 1,
        "Get the state of a setting.",
        "get <variable>\n"
        "Retrieve the state of a setting.",
        CommandGet
    },
    {
        "set",
        TRUE, 1, 10,
        "Set the state of a setting.",
        "Set <variable> <args>...\n"
        "Set the state of a setting.",
        CommandSet
    },
    {
        "vars",
        FALSE, 0, 0,
        "List available settings/variables",
        "List all available variables and whether they are read only.",
        CommandVars
    },                                  
    {NULL, FALSE, 0, 0, NULL,NULL}
};

static char *args[MAX_ARGS];
static bool quit = FALSE;
static List_t *CommandsList;
static List_t *VariableHandlers;
static pthread_mutex_t CommandMutex = PTHREAD_MUTEX_INITIALIZER;

static char COMMAND[] = "Command";

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/

int CommandInit(void)
{
    rl_readline_name = "DVBStreamer";
    rl_attempted_completion_function = AttemptComplete;
    CommandsList = ListCreate();
    if (!CommandsList)
    {
        LogModule(LOG_ERROR, COMMAND, "Failed to allocate CommandsList!\n");
        return -1;
    }

    VariableHandlers = ListCreate();
    if (!VariableHandlers)
    {
        LogModule(LOG_ERROR, COMMAND, "Failed to allocate VariableHandlers!\n");
        return -1;
    }
    
    ListAdd( CommandsList, coreCommands);

    CommandInstallPids();
    CommandInstallServiceFilter();
    CommandInstallInfo();    
    CommandInstallScanning();

    return 0;
}

void CommandDeInit(void)
{
    CommandUnInstallPids();
    CommandUnInstallServiceFilter();
    CommandUnInstallInfo();    
    CommandUnInstallScanning();
    ListFree( CommandsList, NULL);
    ListFree( VariableHandlers, NULL);
    
}

void CommandRegisterCommands(Command_t *commands)
{
    ListAdd( CommandsList, commands);
}

void CommandUnRegisterCommands(Command_t *commands)
{
    ListRemove( CommandsList, commands);
}

void CommandRegisterVariable(CommandVariable_t *handler)
{
    ListAdd(VariableHandlers, handler);
}

void CommandUnRegisterVariable(CommandVariable_t *handler)
{
    ListRemove(VariableHandlers, handler);
}

/*******************************************************************************
* Command Loop/Startup file functions                                          *
*******************************************************************************/
void CommandLoop(void)
{
    quit = FALSE;

    /* Start Command loop */
    while(!quit && !ExitProgram)
    {
        char *line = readline(PROMPT);
        if (line)
        {
            if (!CommandExecuteConsole(line))
            {
                printf("Unknown command \"%s\"\n", line);
            }
            free(line);
        }
    }
}

int CommandProcessFile(char *file)
{
    int lineno = 0;
    FILE *fp;
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
        if (!CommandExecuteConsole(line))
        {
            fprintf(stderr, "%s(%d): Unknown command \"%s\"\n", file, lineno, line);
        }
    }

    fclose(fp);
    return 0;
}

bool CommandExecuteConsole(char *line)
{
    bool found = FALSE;
    if (CommandExecute(&ConsoleCommandContext, printf, line))
    {
        if (ConsoleCommandContext.errorNumber != COMMAND_OK)
        {
            printf("%s\n", ConsoleCommandContext.errorMessage);
        }
        add_history(line);
        found = TRUE;
    }
    return found;
}

bool CommandExecute(CommandContext_t *context, int (*cmdprintf)(const char *, ...), char *line)
{
    bool commandFound = TRUE;
    char *command = NULL;
    char *argument = NULL;

    pthread_mutex_lock(&CommandMutex);
    
    CurrentCommandContext = context;
    CommandPrintf = cmdprintf;

    CommandError(COMMAND_OK, "OK");
    ParseLine(line, &command, &argument);
    
    if (command && (strlen(command) > 0))
    {
        commandFound = ProcessCommand(command, argument);
        free(command);
        if (argument)
        {
            free(argument);
        }
        if (!commandFound)
        {
            CommandError(COMMAND_ERROR_UNKNOWN_COMMAND, "Unknown command");
        }
    }
    
    pthread_mutex_unlock(&CommandMutex);
    
    return commandFound;
}

/*******************************************************************************
* Command parsing functions                                                    *
*******************************************************************************/
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
    static ListIterator_t iterator;
    int i;

    if (state == 0)
    {
        lastIndex = -1;
        textlen = strlen(text);
        ListIterator_Init(iterator, CommandsList);
    }

    while(ListIterator_MoreEntries(iterator))
    {
        Command_t *commands = ListIterator_Current(iterator);
        for ( i = lastIndex + 1; commands[i].command; i ++)
        {
            if (strncasecmp(text, commands[i].command, textlen) == 0)
            {
                lastIndex = i;
                return strdup(commands[i].command);
            }
        }
        ListIterator_Next(iterator);
        lastIndex = -1;
    }

    return NULL;
}


static bool ProcessCommand(char *command, char *argument)
{
    char **argv = NULL;
    int argc = 0;
    bool commandFound = FALSE;
    ListIterator_t iterator;
    Command_t *commandInfo = NULL;

    for ( ListIterator_Init(iterator, CommandsList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        Command_t *commands = ListIterator_Current(iterator);
        commandInfo = FindCommand(commands, command);
        if (commandInfo)
        {
            break;
        }
    }
    
    if (!commandInfo && CurrentCommandContext->commands)
    {
        commandInfo = FindCommand(CurrentCommandContext->commands, command);
    }
    
    if (commandInfo)
    {
        if (argument)
        {
            if (commandInfo->tokenise)
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
    
        if ((argc >= commandInfo->minArgs) && (argc <= commandInfo->maxArgs))
        {
            commandInfo->commandfunc(argc, argv );
        }
        else
        {
            CommandError(COMMAND_ERROR_WRONG_ARGS, "Incorrect number of arguments!");
        }
    
        if (commandInfo->tokenise)
        {
            int a;
    
            /* Free the arguments but not the array as that is a static array */
            for (a = 0; a < argc; a ++)
            {
                free(argv[a]);
            }
        }
    
        commandFound = TRUE;
    }
    
    return commandFound;
}

static Command_t *FindCommand(Command_t *commands, char *command)
{
    int i;
    for (i = 0; commands[i].command; i ++)
    {
        if (strcasecmp(command,commands[i].command) == 0)
        {
            return &commands[i];
        }
    }
    return NULL;
}

static void ParseLine(char *line, char **command, char **argument)
{
    long eol = 0;
    long eoc = 0;
    char *resultCmd = NULL;
    char *resultArg = NULL;

    /* Trim leading spaces */
    for (;*line && isspace(*line); line ++);

    /* Handle null/comment lines */
    if (*line == '#')
    {
        return;
    }

    /* Handle end of line comments */
    for (eol = 0; line[eol] && (line[eol] != '#'); eol ++);
    
    /* Find first space after command */
    for (eoc = 0; line[eoc] && (eoc < eol); eoc ++)
    {
        if (isspace(line[eoc]))
        {
            break;
        }
    }
   
    if (eoc != eol)
    {
        long argStart;
        long argEnd;
        long argLen;
                
        for (argStart = eoc + 1;(argStart < eol) && isspace(line[argStart]); argStart ++);

        for (argEnd = eol - 1; (argEnd > argStart) && isspace(line[argEnd]); argEnd --);

        argLen = (argEnd - argStart) + 1;
        if (argLen > 0)
        {
            resultArg = malloc(argLen + 1);
            if (resultArg)
            {
                strncpy(resultArg, line + argStart, argLen);
                resultArg[argLen] = 0;
            }
        }
    }
    if(eoc)
    {
        resultCmd = malloc(eoc + 1);
        if (resultCmd)
        {
            strncpy(resultCmd, line, eoc);
            resultCmd[eoc] = 0;
        }
    }
    *command = resultCmd;
    *argument = resultArg;
}

static char **Tokenise(char *arguments, int *argc)
{
    int currentarg = 0;
    char *start = arguments;
    char *end;
    char t;
   
    
    while (*start)
    {
        bool quotesOpen = FALSE;
        
        /* Trim spaces from the start */
        for (; *start && isspace(*start); start ++);
        
        if (start[0] == '"')
        {
            quotesOpen = TRUE;
            start ++;
        }
        
        /* Work out the end of the argument */
        for (end = start; *end; end ++)
        {
            if (quotesOpen)
            {
                if (*end== '"')
                {
                    break;
                }
            }
            else if (isspace(*end))
            {
                break;
            }
        }

        t = end[0];
        end[0] = 0;
        args[currentarg] = strdup(start);
        end[0] = t;
        start = end;
        if (quotesOpen)
        {
            start ++;
        }
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


/*******************************************************************************
* Command Functions                                                            *
*******************************************************************************/
static void CommandQuit(int argc, char **argv)
{
    if (CurrentCommandContext->remote)
    {
        CommandError(COMMAND_ERROR_GENERIC, "Only console sessions can do that!");
    }
    else
    {
        quit = TRUE;
    }
}


static void CommandSelect(int argc, char **argv)
{
    Service_t *service;

    CommandCheckAuthenticated();

    service = TuningCurrentServiceSet(argv[0]);
    if (service)
    {
        CommandPrintf("Name      = %s\n", service->name);
        CommandPrintf("ID        = %04x\n", service->id);
        ServiceRefDec(service);
    }
    else
    {
        CommandError(COMMAND_ERROR_GENERIC, "Service not found!");
    }
}


static void CommandHelp(int argc, char **argv)
{
    int i;
    ListIterator_t iterator;

    if (argc)
    {
        Command_t *requestedcmd = NULL;
        for ( ListIterator_Init(iterator, CommandsList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
        {
            Command_t *commands = ListIterator_Current(iterator);
            requestedcmd = FindCommand( commands, argv[0]);
            if (requestedcmd)
            {
                break;
            }
        }
        
        if (!requestedcmd && CurrentCommandContext->commands)
        {
            requestedcmd = FindCommand( CurrentCommandContext->commands, argv[0]);
        }
        
        if (requestedcmd)
        {
            CommandPrintf("%s\n\n", requestedcmd->longHelp);
        }
        else
        {
            CommandError(COMMAND_ERROR_GENERIC,"No help for unknown command!");
        }
    }
    else
    {
        for ( ListIterator_Init(iterator, CommandsList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
        {
            Command_t *commands = ListIterator_Current(iterator);
            for (i = 0; commands[i].command; i ++)
            {
                CommandPrintf("%12s - %s\n", commands[i].command, commands[i].shortHelp);
            }
        }
        
        if (CurrentCommandContext->commands)
        {
            for (i = 0; CurrentCommandContext->commands[i].command; i ++)
            {
                CommandPrintf("%12s - %s\n", CurrentCommandContext->commands[i].command,
                              CurrentCommandContext->commands[i].shortHelp);
            }
        }
        
        
    }
}

static void CommandGet(int argc, char **argv)
{
    ListIterator_t iterator;
    bool found = FALSE;
    for (ListIterator_Init(iterator, VariableHandlers); 
         ListIterator_MoreEntries(iterator);
         ListIterator_Next(iterator))
    {
        CommandVariable_t *handler = (CommandVariable_t*)ListIterator_Current(iterator);
        if (strcmp(handler->name, argv[0]) == 0)
        {
            if (handler->get)
            {
                handler->get(handler->name);
            }
            else
            {
                CommandError(COMMAND_ERROR_GENERIC, "Variable \"%s\" is write-only!\n", handler->name);
            }
            found = TRUE;
        }
    }

    if (!found)
    {
        CommandError(COMMAND_ERROR_GENERIC, "Unknown variable \"%s\"", argv[0]);
    }
}

static void CommandSet(int argc, char **argv)
{
    ListIterator_t iterator;
    bool found = FALSE;
    for (ListIterator_Init(iterator, VariableHandlers); 
         ListIterator_MoreEntries(iterator);
         ListIterator_Next(iterator))
    {
        CommandVariable_t *handler = (CommandVariable_t*)ListIterator_Current(iterator);
        if (strcmp(handler->name, argv[0]) == 0)
        {
            if (handler->set)
            {
                handler->set(handler->name, argc-1, argv + 1);
            }
            else
            {
                CommandError(COMMAND_ERROR_GENERIC, "Variable \"%s\" is read-only!\n", handler->name);
            }
            found = TRUE;            
        }
    }
    if (!found)
    {
        CommandError(COMMAND_ERROR_GENERIC, "Unknown variable \"%s\"", argv[0]);
    }
         
}

static void CommandVars(int argc, char **argv)
{
    ListIterator_t iterator;

    for (ListIterator_Init(iterator, VariableHandlers); 
         ListIterator_MoreEntries(iterator);
         ListIterator_Next(iterator))
    {
        CommandVariable_t *handler = (CommandVariable_t*)ListIterator_Current(iterator);
        if (handler->get && handler->set)
        {
            CommandPrintf(" %s\n", handler->name);
        }
        else if (handler->get)
        {
            CommandPrintf(" %s (Read-only)\n", handler->name);            
        }
        else if (handler->set)
        {
            CommandPrintf(" %s (Write-only)\n", handler->name);            
        }
     }
}
