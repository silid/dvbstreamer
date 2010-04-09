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
#include <unistd.h>

#include "main.h"
#include "logging.h"
#include "pthread.h"

/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/
#define MAX_THREADS 100

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void LoggingInitCommon(int logLevel);
static void LogImpl(int level, const char *module, const char * format, va_list valist);
static char *LogGetThreadName(pthread_t thread);
/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/
typedef struct ThreadName_s
{
    pthread_t thread;
    char *name;
}ThreadName_t;

typedef struct ModuleLevel_s
{
    char *module;
    int level;
    struct struct ModuleLevel_s *next;
}ModuleLevel_t;

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
static ThreadName_t threadNames[MAX_THREADS];
static ModuleLevel_t *moduleLevels = NULL;

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
        logFP = fopen(filepath, "a");
        if (logFP == NULL)
        {
            return -1;
        }

        /* Turn off buffering */
        setbuf(logFP, NULL);
    }
    
    LoggingInitCommon(logLevel);
    return 0;
}

int LoggingInit(char *filename, int logLevel)
{
  char *logFile=calloc(PATH_MAX,1);

    /* Try /var/log first then users home directory */
    sprintf(logFile, "/var/log/%s", filename);
    logFP = fopen(logFile, "a");
    if (logFP == NULL)
    {
        sprintf(logFile, "%s/%s", DataDirectory, filename);
        logFP = fopen(logFile, "a");
    }
    if (logFP == NULL)
    {
        return -1;
    }
    /* Turn off buffering */
    setbuf(logFP, NULL);

    LoggingInitCommon(logLevel);    
    return 0;
}

void LoggingRedirectStdErrStdOut(void)
{
    dup2(fileno(logFP), 1);
    dup2(fileno(logFP), 2);
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

void LogRegisterThread(pthread_t thread, const char *name)
{
    int i;
    pthread_mutex_lock(&mutex);
    for (i = 0; i < MAX_THREADS; i ++)
    {
        if (threadNames[i].thread == 0)
        {
            threadNames[i].thread = thread;
            threadNames[i].name = strdup(name);
            break;
        }
    }
    pthread_mutex_unlock(&mutex);
}

void LogUnregisterThread(pthread_t thread)
{
    int i;
    pthread_mutex_lock(&mutex);
    for (i = 0; i < MAX_THREADS; i ++)
    {
        if (threadNames[i].thread == thread)
        {
            threadNames[i].thread = 0;
            free(threadNames[i].name);
            break;
        }
    }
    pthread_mutex_unlock(&mutex);
}

void LogModule(int level, const char *module, char *format, ...)
{
    va_list valist;

    if (level == 0)
    {
        va_start(valist, format);
        vfprintf(stderr, format, valist);
        va_end(valist);
        if (strchr(format, '\n') == NULL)
        {
            fprintf(stderr, "\n");
        }
    }
    level = LogGetModuleLevel(module, level);
    if (level <= verbosity)
    {
        va_start(valist, format);
        LogImpl(level, module, format, valist);
        va_end(valist);
    }
    
}
/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/
static void LoggingInitCommon(int logLevel)
{
    fprintf(logFP, "------------------- | --------------- | -- | --------------- | ----------------------------------------\n");
    fprintf(logFP, "Date       Time     | Module          | Lv | Thread          | Details\n");
    fprintf(logFP, "------------------- | --------------- | -- | --------------- | ----------------------------------------\n");
    verbosity = logLevel;
    memset(&threadNames, 0, sizeof(threadNames));
}

static void LogImpl(int level, const char *module, const char * format, va_list valist)
{
    char timeBuffer[24]; /* "YYYY-MM-DD HH:MM:SS" */
    time_t curtime;
    struct tm *loctime;
    char *thread;
    
    pthread_mutex_lock(&mutex);

    /* Get the current time. */
    curtime = time (NULL);
    /* Convert it to local time representation. */
    loctime = localtime (&curtime);
    /* Print it out in a nice format. */
    strftime (timeBuffer, sizeof(timeBuffer), "%F %T", loctime);

    thread = LogGetThreadName(pthread_self());
    
    fprintf(logFP, "%-19s | %-15s | %2d | %-15s | ", timeBuffer, module ? module:"<Unknown>", level, thread);

    vfprintf(logFP, format, valist);

    if (strchr(format, '\n') == NULL)
    {
        fprintf(logFP, "\n");
    }

    if ((level == LOG_ERROR) && (errno != 0))
    {
        fprintf(logFP, "%-19s | %-15s | %2d | %-15s | errno = %d (%s)\n", timeBuffer,
            module ? module:"<Unknown>", level, thread, errno, strerror(errno));
    }

    pthread_mutex_unlock(&mutex);
}

static char *LogGetThreadName(pthread_t thread)
{
    static char numericName[20];
    int i;
    for (i = 0; i < MAX_THREADS; i ++)
    {
        if (thread == threadNames[i].thread)
        {
            return threadNames[i].name;
        }
    }
    sprintf(numericName, "0x%08lx", (unsigned long)thread);
    return numericName;
}

static int LogGetModuleLevel(const char *module, int level)
{
    int result = level;
    ModuleLevel_t *modLevel;
    for (modLevel = moduleLevels; modLevel; modLevel = modLevel->next)
    {
        if (strcmp(modLevel->module, module) == 0)
        {
            result = (level < modLevel->level) ? level: modLevel->level;
            break;
        }
    }
    return result;
}
