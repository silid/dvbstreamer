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
 * Current verbosity level.
 * Used to determine when to send text from a printlog call to the log output.
 */
extern int verbosity;

/**
 * Write the text describe by format to the log output, if the current verbosity
 * level is greater or equal to level.
 * @param level The level at which to output this text.
 * @param foramt String in printf format to output.
 */
extern void printlog(int level, char *format, ...);
#endif
