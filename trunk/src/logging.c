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
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <time.h>

#include "main.h"
#include "logging.h"
#include "pthread.h"

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
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static FILE *logFP = NULL;

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
int LoggingInitFile(char *filepath, int logLevel)
{
    if (strcmp(filepath, "-") == 0)
    {
        logFP = stderr;
    }
    else
    {
        logFP = freopen(filepath, "a", stderr);
        if (logFP == NULL)
        {
            return -1;
        }
        
        /* Turn off buffering */
        setbuf(logFP, NULL);
    }
    verbosity = logLevel;
    return 0;
}

int LoggingInit(char *filename, int logLevel)
{
    char logFile[PATH_MAX];
    
    /* Try /var/log first then users home directory */
    sprintf(logFile, "/var/log/%s", filename);
    logFP = freopen(logFile, "a", stderr);
    if (logFP == NULL)
    {
        sprintf(logFile, "%s/%s", DataDirectory, filename);
        logFP = freopen(logFile, "a", stderr);
    }
    if (logFP == NULL)
    {
        return -1;
    }
    /* Turn off buffering */
    setbuf(logFP, NULL);

    verbosity = logLevel;
    return 0;
}
    
void LoggingDeInit(void)
{
    fclose(logFP);
}

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
    va_list valist;
    va_start(valist, format);
    if (level == 0)
    {
        vfprintf(stderr, format, valist);
    }
    if (level <= verbosity)
    {
        LogImpl(level, module, format, valist);
    }
    va_end(valist);
}
/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/

static void LogImpl(int level, const char *module, const char * format, va_list valist)
{
    char timeBuffer[24]; /* "YYYY-MM-DD HH:MM:SS" */
    time_t curtime;
    struct tm *loctime;

    pthread_mutex_lock(&mutex);

    /* Get the current time. */
    curtime = time (NULL);
    /* Convert it to local time representation. */
    loctime = localtime (&curtime);
    /* Print it out in a nice format. */
    strftime (timeBuffer, sizeof(timeBuffer), "%F %T : ", loctime);

    fprintf(logFP, "%s %-15s : %2d : ", timeBuffer, module ? module:"<Unknown>", level);

    vfprintf(logFP, format, valist);

    if ((level == LOG_ERROR) && (errno != 0))
    {
        fprintf(logFP, "%s %-15s : %2d : errno = %d (%s)\n", timeBuffer, 
            module ? module:"<Unknown>", level, errno, strerror(errno));
    }

    pthread_mutex_unlock(&mutex);
}
