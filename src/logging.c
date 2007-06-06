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

Logging functions.

*/
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

#include "main.h"
#include "logging.h"

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/

static void LogImpl(int level, const char *module, const char * format, va_list valist);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/

/**
 * Current verbosity level.
 * Used to determine when to send text from a printlog call to the log output.
 */
static int verbosity = 0;

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/

void LogLevelSet(int level)
{
    verbosity = level;
}

int LogLevelGet(void)
{
    return verbosity;
}

void LogLevelInc(void)
{
    verbosity ++;
}

void LogLevelDec(void)
{
    verbosity ++;
}

bool LogLevelIsEnabled(int level)
{
    return (level <= verbosity);
}

void LogModule(int level, const char *module, char *format, ...)
{
    if (level <= verbosity)
    {
        va_list valist;
        va_start(valist, format);
        LogImpl(level, module, format, valist);
        va_end(valist);
    }
}
/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/

static void LogImpl(int level, const char *module, const char * format, va_list valist)
{
    
#ifdef LOGGING_CHECK_DAEMON
    if (DaemonMode)
    {
        char buffer[24]; /* "YYYY-MM-DD HH:MM:SS" */
        time_t curtime;
        struct tm *loctime;
        /* Get the current time. */
        curtime = time (NULL);
        /* Convert it to local time representation. */
        loctime = localtime (&curtime);
        /* Print it out in a nice format. */
        strftime (buffer, sizeof(buffer), "%F %T : ", loctime);
        fputs(buffer, stderr);
    }
#endif

    fprintf(stderr, "%-15s : ", module ? module:"<Unknown>");

    vfprintf(stderr, format, valist);

#ifdef LOGGING_CHECK_DAEMON        
    if (DaemonMode)
    {
        fflush(stderr);
    }
#endif        
}
