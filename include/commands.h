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

commands.h

Command Processing and command functions.

*/
#ifndef _COMMANDS_H
#define _COMMANDS_H
#include <stdint.h>
#include "types.h"
/**
 * @defgroup Commands Command Processing
 *@{
 */

#define COMMAND_OK                    0x0000
#define COMMAND_ERROR_TOO_MANY_CONNS  0x0001
#define COMMAND_ERROR_UNKNOWN_COMMAND 0x0002
#define COMMAND_ERROR_WRONG_ARGS      0x0003
#define COMMAND_ERROR_AUTHENTICATION  0x0004
#define COMMAND_ERROR_GENERIC         0xffff

#define MAX_ERR_MSG 256
/**
 * Structure used to define a command.
 */
typedef struct Command_t
{
    char *command;  /**< Command name */
    bool  tokenise; /**< Should the argument string be split on spaces? */
    int   minArgs;  /**< Minimum number of args this command accepts. */
    int   maxArgs;  /**< Maximum number of args this command accepts. */
    char *shortHelp;/**< Short description of the command, displayed by help */
    char *longHelp; /**< Long description of the command, displayed by help <command> */
    void (*commandfunc)(int argc, char **argv); /**< Function to call to execute command */
}Command_t;

/**
 * Function pointer to call when retrieve a variable.
 */
typedef void (*CommandVariableGet_t)(char *name);
/**
 * Function pointer to call when setting a variable.
 */
typedef void (*CommandVariableSet_t)(char *name, int argc, char **argv);

/**
 * Structure used to define a variable that can be retrieved/set item.
 */
typedef struct CommandVariable_t
{
    char *name;               /**< Name of the variable item as passed to the 
                                   get/set command. This must not include space 
                                   characters! */
    char *description;        /**< Short description of the variable. */
    CommandVariableGet_t get; /**< Function to call when the get is requested.
                                   (May be NULL) */
    CommandVariableSet_t set; /**< Function to call when the get is requested. 
                                   (May be NULL) */
}CommandVariable_t;


/**
 * Structure used to define the context a command is running in.
 */
typedef struct CommandContext_t
{
    char *interface;     /**< Human readable string containing the interface name,
                              ie Console for console or an IP address if a remote connection.*/
    bool remote;         /**< Whether this is a remote connection, ie not via the console. */
    void *privateArg;    /**< Private pointer for use by the owner of the context. */
    Command_t *commands; /**< Commands specific to this context */
    bool authenticated;  /**< Whether this context has been authenticated against the admin username/password.*/
    uint16_t errorNumber;    /**< Error number from the last command executed. */
    char errorMessage[MAX_ERR_MSG]; /**< Error message text from the last command executed. */
}CommandContext_t;

/**
 * Macro for making error reporting simpler and consistent.
 */
#define CommandError(_errcode, _msgformat...) \
    do{\
        CurrentCommandContext->errorNumber = _errcode;\
        snprintf(CurrentCommandContext->errorMessage, MAX_ERR_MSG,_msgformat);\
    }while(0)

/**
 * Macro to make checking a user has authenticated before executing a command
 * easier and consistent.
 */
#define CommandCheckAuthenticated() \
    do{\
        if (!CurrentCommandContext->authenticated)\
        {\
            CommandError(COMMAND_ERROR_AUTHENTICATION, "Not authenticated!");\
            return;\
        }\
    }while(0)

/**
 * Initialise the command processor.
 * @return 0 on success, non 0 on error.
 */
int CommandInit(void);

/**
 * Deinitialise the command processor.
 */
void CommandDeInit(void);

/**
 * Register an array of commands to be used by the command processor.
 * @param commands The command=NULL terminated array of commands to add,
 */
void CommandRegisterCommands(Command_t *commands);

/**
 * Unregister an array of commands previously registered by a call to
 * CommandRegisterCommands.
 * @param commands The array of commands to remove.
 */
void CommandUnRegisterCommands(Command_t *commands);

/**
 * Start interactive command loop.
 * This function returns when the user exits the command loop, or the ExitProgram variable is TRUE.
 */
void CommandLoop(void);

/**
 * Load and process the command in file.
 * @param file Name of the file to process.
 * @return 0 on success, non 0 on error.
 */
int CommandProcessFile(char *file);

/**
 * Execute a command in the console command context.
 * @param line The command line to execute.
 * @return TRUE if the command was found, FALSE otherwise.
 */
bool CommandExecuteConsole(char *line);

/**
 * Execute the command line supplied.
 * @param context The context the command is being executed.
 * @param cmdprintf Function pointer to set CommandPrintf to.
 * @param command The command line to execute.
 * @return true if the command was found, false otherwise.
 */
bool CommandExecute(CommandContext_t *context, int (*cmdprintf)(const char *, ...), char *command);

/**
 * Printf style output function that should be in command functions to send
 * data to the user.
 * @param fmt Printf format.
 * @return Number of bytes printed.
 */
int (*CommandPrintf)(const char *fmt, ...);

/**
 * Context the currently running command is executing in.
 */
extern CommandContext_t *CurrentCommandContext;

/**
 * Register an info handler that will be invoked by the get/set command.
 * @param handler The details of the info handler.
 */
void CommandRegisterVariable(CommandVariable_t *handler);

/**
 * UnRegister a variable handler that will be invoked by the get/set command.
 * @param handler The details of the variable handler.
 */
void CommandUnRegisterVariable(CommandVariable_t *handler);
/** @} */
#endif
