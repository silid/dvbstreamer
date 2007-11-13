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

queryepg.c

Plugin to query the EPG Database.

*/
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "main.h"
#include "utf8.h"
#include "plugin.h"
#include "epgdbase.h"
#include "dbase.h"

#include "list.h"
#include "logging.h"


/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void CommandListEvents(int argc, char **argv);
static void CommandEventDetails(int argc, char **argv);

static void OutputServiceEvents(Multiplex_t *multiplex, Service_t *service, time_t startTime, time_t endTime);
static time_t ParseTime(char *timeStr);
static bool FilterEvent(time_t startTime, time_t endTime, EPGEvent_t *event);

/*******************************************************************************
* Plugin Setup                                                                 *
*******************************************************************************/
#ifdef __CYGWIN__
#define PluginInterface QueryEPGPluginInterface
#endif

PLUGIN_COMMANDS(
    {
        "lsevents",
        TRUE, 0, 6,
        "List events.",
        "lsevents [sn <Service Name>] [st <start time>] [et <end time>]\n"
        "List the events for either all channels or the specified channel which fall within the specified start and end times or all known events.\n"
        "The start and end times are in the format YYYYMMDDhhmm.\n\n"
        "For example, to list all the events for BBC ONE between 1st November 2007 12:00 and 2nd November 2007 12:00:\n"
        "lsevents sn \"BBC ONE\" st 200711011200 et 200711021200\n",
        CommandListEvents
    },
    {
        "eventinfo",
        TRUE, 1, 3,
        "Retrieve information on the specified event.",
        "eventinfo <event id> <detail name> [<lang>]\n"
        "Retrieve information on the specified event.\n"
        "detail name can be one of the following:\n"
        "    title       - Title of the event.\n"
        "    description - Description of the event.\n"
        "If no detail name is given all available detail names are printed.\n"
        "lang show be in ISO639 format or '---' if not language specific.\n"
        "If no language is given all available languages will be displayed.\n",
        CommandEventDetails
    }
);

PLUGIN_INTERFACE_C(
    PLUGIN_FOR_ALL,
    "QueryEPG", "0.1", 
    "Plugin to query the EPG Database.", 
    "charrea6@users.sourceforge.net"
    );

/*******************************************************************************
* Command Functions                                                            *
*******************************************************************************/
static void CommandListEvents(int argc, char **argv)
{
    char *startTimeStr = NULL;
    char *endTimeStr = "203712312359";
    time_t startTime = 0;
    time_t endTime = 0;
    char *serviceName = NULL;
    int i;
    /* Process arguments */
    for (i = 0; i < argc; i ++)
    {
        if (strcmp("sn", argv[i]) == 0)
        {
            if (i + 1 < argc)
            {
                serviceName = argv[i + 1];
                i ++;
            }
            else
            {
                CommandError(COMMAND_ERROR_WRONG_ARGS, "Missing service name!");
                return;
            }
        }
        else if (strcmp("st", argv[i]) == 0)
        {
            if (i + 1 < argc)
            {
                startTimeStr = argv[i + 1];
                i ++;
            }
            else
            {
                CommandError(COMMAND_ERROR_WRONG_ARGS, "Missing start time!");
                return;
            }
        }
        else if (strcmp("et", argv[i]) == 0)
        {
            if (i + 1 < argc)
            {
                endTimeStr = argv[i + 1];
                i ++;
            }
            else
            {
                CommandError(COMMAND_ERROR_WRONG_ARGS, "Missing end time!");
                return;
            }
        }
        else
        {
            CommandError(COMMAND_ERROR_WRONG_ARGS, "Unknown argument!");
            return;
        }
    }
    /* Parse the time variables */
    if (startTimeStr == NULL)
    {
        time(&startTime);
    }
    else
    {
        startTime = ParseTime(startTimeStr);
        if (startTime == -1)
        {
            CommandError(COMMAND_ERROR_GENERIC, "Failed to parse start time!");
            return; 
        }
    }
    
    endTime = ParseTime(endTimeStr);
    if (endTime == -1)

    {
        CommandError(COMMAND_ERROR_GENERIC, "Failed to parse end time!");
        return; 
    }

    /* Search for events */
    EPGDBaseTransactionStart();    
    if (serviceName)
    {
        Multiplex_t *mux;
        Service_t *service;
        service = ServiceFind(serviceName);
        if (service)
        {
            mux = MultiplexFindUID(service->multiplexUID);

            OutputServiceEvents(mux, service, startTime, endTime);
            
            MultiplexRefDec(mux);
            ServiceRefDec(service);
        }
        else
        {
            CommandError(COMMAND_ERROR_GENERIC, "Failed to find service \"%s\"", serviceName);
        }
    }
    else
    {
        Multiplex_t *mux;
        MultiplexEnumerator_t enumerator = MultiplexEnumeratorGet();
        
        do
        {
            mux = MultiplexGetNext(enumerator);
            if (mux)
            {
                Service_t *service;
                ServiceEnumerator_t serviceEnumerator = ServiceEnumeratorForMultiplex(mux);
                do
                {
                    service = ServiceGetNext(serviceEnumerator);
                    if (service)
                    {
                        OutputServiceEvents(mux, service, startTime, endTime);
                        ServiceRefDec(service);
                    }
                }while(service && !ExitProgram);
                MultiplexRefDec(mux);
            }
        }while(mux && !ExitProgram);
        
    MultiplexEnumeratorDestroy(enumerator);
    }
    EPGDBaseTransactionCommit();    
}

static void CommandEventDetails(int argc, char **argv)
{
    EPGDBaseEnumerator_t enumerator;
    EPGEventDetail_t *detail;
    EPGServiceRef_t serviceRef;
    unsigned int eventId;
    bool displayLangs = (argc == 2);
    bool displayDetails = (argc == 1);
    
    if (sscanf(argv[0], "%04x.%04x.%04x.%04x", &serviceRef.netId, &serviceRef.tsId, &serviceRef.serviceId, &eventId) != 4)
    {
        CommandError(COMMAND_ERROR_GENERIC, "Failed to parse event id!");
        return;
    }

    if (displayDetails)
    {
        enumerator = EPGDBaseDetailEnumeratorGet(&serviceRef, eventId);
    }
    else
    {
        enumerator = EPGDBaseDetailGet(&serviceRef, eventId, argv[1]);
    }
    do
    {
        detail = EPGDBaseDetailGetNext(enumerator);
        if (detail)
        {
            if (displayDetails)
            {
                CommandPrintf("%s\n", detail->name);
            }
            else if (displayLangs)
            {
                CommandPrintf("%s\n", detail->lang);                
            }
            else
            {
                CommandPrintf("%s\n", detail->value);
            }
            ObjectRefDec(detail);
        }
    }while(detail && !ExitProgram);
    
    EPGDBaseEnumeratorDestroy(enumerator);
    
}


static void OutputServiceEvents(Multiplex_t *multiplex, Service_t *service, time_t startTime, time_t endTime)
{
    EPGEvent_t *event;
    EPGDBaseEnumerator_t enumerator;
    EPGServiceRef_t serviceRef;

    serviceRef.netId = multiplex->networkId;
    serviceRef.tsId = multiplex->tsId;
    serviceRef.serviceId = service->source;

    enumerator = EPGDBaseEventEnumeratorGetService(&serviceRef);
    do
    {
        event = EPGDBaseEventGetNext(enumerator);
        if (event)
        {
            
            if (FilterEvent(startTime, endTime, event))
            {
                CommandPrintf("%04x.%04x.%04x.%04x %04d%02d%02d%02d%02d%02d %04d%02d%02d%02d%02d%02d %s\n",
                    serviceRef.netId, serviceRef.tsId, serviceRef.serviceId, event->eventId,
                    event->startTime.tm_year + 1900, event->startTime.tm_mon + 1, event->startTime.tm_mday,
                    event->startTime.tm_hour, event->startTime.tm_min, event->startTime.tm_sec,
                    event->endTime.tm_year + 1900, event->endTime.tm_mon + 1, event->endTime.tm_mday,
                    event->endTime.tm_hour, event->endTime.tm_min, event->endTime.tm_sec,
                    event->ca ? "ca":"fta");
                    
            }
            ObjectRefDec(event);
        }
    }while(event && !ExitProgram);

    EPGDBaseEnumeratorDestroy(enumerator);
}


/*******************************************************************************
* Helper Functions                                                             *
*******************************************************************************/
static time_t ParseTime(char *timeStr)
{
    struct tm timeTm;
    char tmp[5];

    if (strlen(timeStr) != 12)
    {
        return -1;
    }

    strncpy(tmp, timeStr, 4);
    tmp[4] = 0;
    if (sscanf(tmp, "%d", &timeTm.tm_year) == 0)
    {
        return -1;
    }
    timeTm.tm_year -= 1900;

    strncpy(tmp, timeStr + 4, 2);
    tmp[2] = 0;
    if (sscanf(tmp, "%d", &timeTm.tm_mon) == 0)
    {
        return -1;
    }
    timeTm.tm_mon --;

    strncpy(tmp, timeStr + 6, 2);
    tmp[2] = 0;
    if (sscanf(tmp, "%d", &timeTm.tm_mday) == 0)
    {
        return -1;
    }    

    strncpy(tmp, timeStr + 8, 2);
    tmp[2] = 0;
    if (sscanf(tmp, "%d", &timeTm.tm_hour) == 0)
    {
        return -1;
    } 

    strncpy(tmp, timeStr + 10, 2);
    tmp[2] = 0;
    if (sscanf(tmp, "%d", &timeTm.tm_min) == 0)
    {
        return -1;
    }      

    timeTm.tm_sec = 0;
    timeTm.tm_isdst = 0;
    return mktime(&timeTm);
}

static bool FilterEvent(time_t startTime, time_t endTime, EPGEvent_t *event)
{
    time_t eventStartTime;
    time_t eventEndTime;

    eventStartTime = mktime(&event->startTime);
    eventEndTime = mktime(&event->endTime);
    /* Start during the period describe by startTime - endTime */
    if ((startTime <= eventStartTime) && (endTime > eventStartTime))
    {
        return TRUE;
    }
    /* End during the period describe by startTime - endTime */
    if ((startTime <= eventEndTime) && (endTime > eventEndTime))
    {
        return TRUE;
    }
    /* Start before, finish after the period describe by startTime - endTime */
    if ((startTime >= eventStartTime) && (endTime <= eventEndTime))
    {
        return TRUE;
    }
    return FALSE;
}

