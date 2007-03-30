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

int verbosity = 0;

static void LogImpl(int level, const char *module, const char * format, va_list valist);

/*
 * Print out a log message to stderr depending on verbosity
 */
void printlog(int level, const char *format, ...)
{
    if (level <= verbosity)
    {
        va_list valist;
        va_start(valist, format);
        LogImpl(level, NULL, format, valist);
        va_end(valist);
    }
}

void printlogva(int level, const char * format, va_list valist)
{
    if (level <= verbosity)
    {
        LogImpl(level, NULL, format, valist);
    }
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

static void LogImpl(int level, const char *module, const char * format, va_list valist)
{
    char *logline;
    int len;
    
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
