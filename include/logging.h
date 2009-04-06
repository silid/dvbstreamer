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

logging.h

Logging levels and functions.

*/
#ifndef _LOGGING_H
#define _LOGGING_H
#include <stdarg.h>
#include <pthread.h>
#include "types.h"

/**
 * Error logging level, always printed used for fatal error messages.
 */
#define LOG_ERROR    0

/**
 * Information logging level, used for warnings and other information.
 */
#define LOG_INFO     1

/**
 * Verbose Information logging level, less important than information level but
 * not quite debugging.
 */
#define LOG_INFOV    2 /* Verbose information */

/**
 * Debug Logging Level, useful debugging information.
 */
#define LOG_DEBUG    3

/**
 * Verbose Debugging Level, less useful debugging information.
 */
#define LOG_DEBUGV   4 /* Verbose debugging info */

/**
 * Diarrhee level, lots and lots of pointless text.
 */
#define LOG_DIARRHEA 10

/**
 * @internal 
 * Initialises logging, by first attempting to create the log file in /var/log, 
 * then if unsuccessful in ~/.dvbstreamer
 
 * @param filename Name of the log file to create.
 * @param logLevel The initial logging/verbosity level.
 * @return 0 on success.
 */
int LoggingInit(char *filename, int logLevel);

/**
 * @internal
 * Initialises logging by using the file path specified as the log file.
 * @param filepath File path of the file to use for logging.
 * @param logLevel The initial logging/verbosity level.
 * @return 0 on success.
 */
int LoggingInitFile(char *filename, int logLevel); 
 
/**
 * @internal
 * Deinitialise logging.
 */
void LoggingDeInit(void);

/**
 * Set the current logging level.
 * @param level The new level to set.
 */
void LogLevelSet(int level);

/**
 * Retrieves the current logging level.
 * @return The current logging level.
 */
int LogLevelGet(void);

/**
 * Increase the logging level by 1.
 */
void LogLevelInc(void);

/**
 * Decrease the logging level by 1.
 */
void LogLevelDec(void);

/**
 * Determine if the specified logging level is enabled.
 * @param level The level to check.
 * @return TRUE if the level is enabled, FALSE otherwise.
 */
bool LogLevelIsEnabled(int level);

/**
 * Register a name for a pthread. The name will be used instead of the numeric 
 * id in the log output.
 * @param thread pthread to register.
 * @param name Name of the thread.
 */
void LogRegisterThread(pthread_t thread, const char *name);

/**
 * Unregister a name of a pthread.
 * @param thread pthread to unregister.
 */
void LogUnregisterThread(pthread_t thread);

/**
 * Write the text describe by format to the log output, if the current verbosity
 * level is greater or equal to level.
 * @param level The level at which to output this text.
 * @param module The module that is doing the logging.
 * @param format String in printf format to output.
 */
extern void LogModule(int level, const char *module, char *format, ...);
#endif
