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
/* Context Prototypes. */
static void CommandContextSet(CommandContext_t *context);

/* File Prototypes */
static char **AttemptComplete (const char *text, int start, int end);
static char *CompleteCommand(const char *text, int state);
static bool ProcessCommand(CommandContext_t *context, char *command, char *argument);
static Command_t *FindCommand(Command_t *commands, char *command);
static char **Tokenise(char *arguments, int *argc);
static void ParseLine(char *line, char **command, char **argument);


static void CommandQuit(int argc, char **argv);
static void CommandHelp(int argc, char **argv);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/

static CommandContext_t ConsoleCommandContext =
{
    "console",
    FALSE,
    NULL,
    NULL,
    NULL,
    NULL,
    TRUE,
    0,
    {0}
};

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
        "help",
        TRUE, 0, 1,
        "Display the list of commands or help on a specific command.",
        "help [<command>]\n"
        "List all available commands or displays specific help for the command specifed.",
        CommandHelp
    },
    {NULL, FALSE, 0, 0, NULL,NULL}
};

static bool quit = FALSE;
static List_t *CommandsList;
static pthread_mutex_t CommandMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_key_t commandContextKey;

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

    ListAdd( CommandsList, coreCommands);

    pthread_key_create(&commandContextKey, NULL);

    ConsoleCommandContext.outfp = stdout;    
    ConsoleCommandContext.infp = stdin;    

    return 0;
}

void CommandDeInit(void)
{
    ListFree( CommandsList, NULL);
}

void CommandRegisterCommands(Command_t *commands)
{
    pthread_mutex_lock(&CommandMutex);
    ListAdd( CommandsList, commands);
    pthread_mutex_unlock(&CommandMutex);
}

void CommandUnRegisterCommands(Command_t *commands)
{
    pthread_mutex_lock(&CommandMutex);
    ListRemove( CommandsList, commands);
    pthread_mutex_unlock(&CommandMutex);
}
static void CommandContextSet(CommandContext_t *context)
{
    pthread_setspecific(commandContextKey, context);
}

CommandContext_t *CommandContextGet(void)
{
    return pthread_getspecific(commandContextKey);
}

int CommandPrintf(const char* fmt, ...)
{
    va_list args;
    int result = 0;

    CommandContext_t *context = CommandContextGet();
    va_start(args, fmt);
    result = vfprintf(context->outfp, fmt, args);
    va_end(args);
    return result;
}

char *CommandGets(char *buffer, int len)
{
    CommandContext_t *context = CommandContextGet();
    return fgets(buffer, len, context->infp);
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
            CommandExecuteConsole(line);
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
        if (fgets(line, sizeof(line), fp))
        {
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
            if (strlen(line) > 0)
            {
                CommandExecute(&ConsoleCommandContext, line);

                if (ConsoleCommandContext.errorNumber != COMMAND_OK)
                {
                    if (ConsoleCommandContext.errorNumber == COMMAND_ERROR_UNKNOWN_COMMAND)
                    {
                        fprintf(stderr, "%s(%d): Unknown command \"%s\"\n", file, lineno, line);
                    }
                    else
                    {
                        fprintf(stderr, "%s(%d): %s\n", file, lineno, ConsoleCommandContext.errorMessage);
                    }
                }
            }
        }
    }

    fclose(fp);
    return 0;
}

bool CommandExecuteConsole(char *line)
{
    bool found = FALSE;
    
    if (CommandExecute(&ConsoleCommandContext, line))
    {
        add_history(line);
        found = TRUE;
    }

    if (ConsoleCommandContext.errorNumber != COMMAND_OK)
    {
        printf("%s\n", ConsoleCommandContext.errorMessage);
    }

    return found;
}

bool CommandExecute(CommandContext_t *context, char *line)
{
    bool commandFound = FALSE;
    char *command = NULL;
    char *argument = NULL;

    CommandContextSet(context);

    CommandError(COMMAND_OK, "OK");
    ParseLine(line, &command, &argument);

    if (command && (strlen(command) > 0))
    {
        commandFound = ProcessCommand(context, command, argument);
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

    CommandContextSet(NULL);
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


static bool ProcessCommand(CommandContext_t *context, char *command, char *argument)
{
    char **argv = NULL;
    int argc = 0;
    bool commandFound = FALSE;
    ListIterator_t iterator;
    Command_t *commandInfo = NULL;

    pthread_mutex_lock(&CommandMutex);
    if (context->commands)
    {
        commandInfo = FindCommand(context->commands, command);
    }

    if (!commandInfo)
    {
        for ( ListIterator_Init(iterator, CommandsList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
        {
            Command_t *commands = ListIterator_Current(iterator);
            commandInfo = FindCommand(commands, command);
            if (commandInfo)
            {
                break;
            }
        }
    }
    pthread_mutex_unlock(&CommandMutex);


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
                argv = calloc(sizeof(char*), 1);
                argv[0] = argument;
            }
        }
        else
        {
            argc = 0;
            argv = calloc(sizeof(char*), 1);
            argv[0] = NULL;
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

            for (a = 0; a < argc; a ++)
            {
                free(argv[a]);
            }
            free(argv);
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
    char **args;

    args = calloc(sizeof(char *), MAX_ARGS);

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
            break;
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
    CommandContext_t *context = CommandContextGet();

    if (context->remote)
    {
        CommandError(COMMAND_ERROR_GENERIC, "Only console sessions can do that!");
    }
    else
    {
        quit = TRUE;
    }
}


static void CommandHelp(int argc, char **argv)
{
    int i;
    ListIterator_t iterator;
    CommandContext_t *context = CommandContextGet();

    if (argc)
    {
        Command_t *requestedcmd = NULL;


        if (context->commands)
        {
            requestedcmd = FindCommand( context->commands, argv[0]);
        }

        if (!requestedcmd)
        {
            for ( ListIterator_Init(iterator, CommandsList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
            {
                Command_t *commands = ListIterator_Current(iterator);
                requestedcmd = FindCommand( commands, argv[0]);
                if (requestedcmd)
                {
                    break;
                }
            }
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

        if (context->commands)
        {
            for (i = 0; context->commands[i].command; i ++)
            {
                CommandPrintf("%12s - %s\n", context->commands[i].command,
                              context->commands[i].shortHelp);
            }
        }
    }
}

