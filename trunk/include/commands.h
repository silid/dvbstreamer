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
#include "types.h"
/**
 * @defgroup Commands Command Processing
 *@{
 */

/**
 * Structure used to define a command.
 */
typedef struct Command_t
{
    char *command;  /**< Command name */
    bool  tokenise; /**< Should the argument string be split on spaces? */
    int   minargs;  /**< Minimum number of args this command accepts. */
    int   maxargs;  /**< Maximum number of args this command accepts. */
    char *shorthelp;/**< Short description of the command, displayed by help */
    char *longhelp; /**< Long description of the command, displayed by help <command> */
    void (*commandfunc)(int argc, char **argv); /**< Function to call to execute command */
}Command_t;


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
 * Execute the command line supplied.
 * @param command The command line to execute.
 * @return true if the command was found, false otherwise.
 */
bool CommandExecute(char *command);

/**
 * Printf style output function that should be in command functions to send
 * data to the user.
 * @param fmt Printf format.
 * @return Number of bytes printed.
 */
int (*CommandPrintf)(char *fmt, ...);

/** @} */
#endif
