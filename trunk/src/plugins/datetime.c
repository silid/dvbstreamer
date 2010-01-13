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

datetime.c

Example plugin to print out the date/time from the TDT.

*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdint.h>

#include "logging.h"
#include "plugin.h"
#include "dvbpsi/datetime.h"
#include "dvbpsi/tdttot.h"
#include "dvbpsi/atsc/stt.h"

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void ProcessTDT(dvbpsi_tdt_tot_t *tdt);
static void ProcessSTT(dvbpsi_atsc_stt_t *stt);
static long GetMonotonicTime(void);
static void CommandDateTime(int argc, char **argv);
static void DateTimeInstall(bool installed);
static char *DateTimeEventToString(Event_t event, void *payload);
/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static time_t lastDateTime;
static long lastReceived = 0;
static bool timeReceived = FALSE;

static EventSource_t timeSource;
static Event_t timeReceivedEvent;
/*******************************************************************************
* Plugin Setup                                                                 *
*******************************************************************************/
PLUGIN_FEATURES(
    PLUGIN_FEATURE_INSTALL(DateTimeInstall),
    PLUGIN_FEATURE_TDTPROCESSOR(ProcessTDT),
    PLUGIN_FEATURE_STTPROCESSOR(ProcessSTT)
);

PLUGIN_COMMANDS(
    {
        "date",
        0, 0,
        "Display the last date/time received.",
        "Display the last date/time received.",
        CommandDateTime
    }
);

PLUGIN_INTERFACE_CF(
    PLUGIN_FOR_ALL,
    "Date/Time", 
    "1.0", 
    "Plugin that receives the current date/time from the broadcast stream.", 
    "charrea6@users.sourceforge.net"
);

/*******************************************************************************
* NIT Processing Function                                                      *
*******************************************************************************/
static void ProcessTDT(dvbpsi_tdt_tot_t *tdt)
{
    lastDateTime = timegm(&tdt->t_date_time);
    lastReceived = GetMonotonicTime();
    timeReceived = TRUE;
    EventsFireEventListeners(timeReceivedEvent, (void*)lastDateTime);
}

static void ProcessSTT(dvbpsi_atsc_stt_t *stt)
{
    lastDateTime = dvbpsi_atsc_unix_epoch_offset + stt->i_system_time -  stt->i_gps_utc_offset;
    lastReceived = GetMonotonicTime();
    timeReceived = TRUE;
    EventsFireEventListeners(timeReceivedEvent, (void*)lastDateTime);
}

static long GetMonotonicTime(void)
{
    struct timespec now;
    
    if (clock_gettime(CLOCK_MONOTONIC, &now) == -1)    
    {        
        /* Monotonic time failed use real time instead */        
        clock_gettime(CLOCK_REALTIME, &now);    
    }        

    return (now.tv_sec * 1000) + ((now.tv_nsec)/ 1000000.0);
}

/*******************************************************************************
* Command Functions                                                            *
*******************************************************************************/
static void CommandDateTime(int argc, char **argv)
{
    if (timeReceived)
    {
        CommandPrintf("%s", ctime(&lastDateTime));
        CommandPrintf("Last received %d ms ago.\n", GetMonotonicTime() - lastReceived);
    }
    else
    {
        CommandPrintf("No date/time has been received!\n");
    }
}
/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/
static void DateTimeInstall(bool installed)
{
    if (installed)
    {
        timeSource = EventsRegisterSource("datetime");
        timeReceivedEvent = EventsRegisterEvent(timeSource, "received", DateTimeEventToString);
    }
    else
    {
        EventsUnregisterSource(timeSource);
    }
}

static char *DateTimeEventToString(Event_t event, void *payload)
{
    time_t t = (time_t)payload;
    char *result = NULL;
    asprintf(&result, "Time: %s" /* ctime adds a \n */
                      "Seconds since epoch: %ld\n",
                      ctime(&t),
                      t);
    return result;
}