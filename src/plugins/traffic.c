/*
Copyright (C) 2008  Steve VanDeBogart

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

traffic.c

Plugin to display PID traffic.

*/
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <string.h>

#include "main.h"
#include "plugin.h"
#include "cache.h"
#include "tuning.h"
#include "list.h"

/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/
typedef struct TrafficPIDCount_s {
    uint16_t PID;
    uint16_t count;
    uint16_t oldCount;
} TrafficPIDCount_t;

#define PID_EOL ((1 << 16) - 1)

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void InitFilter(PIDFilter_t *filter);
static void DeinitFilter(PIDFilter_t *filter);

static int FilterPacket(PIDFilter_t *pidfilter, void *arg, uint16_t pid, TSPacket_t *packet);
static TSPacket_t* ProcessPacket(PIDFilter_t *pidfilter, void *arg, TSPacket_t *packet);
static void HandleMuxChange(struct PIDFilter_s *pidfilter, void *userarg, Multiplex_t *multiplex);
static void RotateData(void);
static TrafficPIDCount_t * CopyPIDCounts(void);

static void CommandTraffic(int argc, char **argv);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static PluginFilter_t TrafficFilter = {NULL, InitFilter, DeinitFilter};

static pthread_mutex_t TrafficMutex = PTHREAD_MUTEX_INITIALIZER;

static List_t * PIDCounts = NULL;
static int pidListLength = 0;
static struct timeval currentStart;
static struct timeval lastStart;
static bool serviceLock;

/*******************************************************************************
* Plugin Setup                                                                 *
*******************************************************************************/
#ifdef __CYGWIN__
#define PluginInterface TrafficPluginInterface
#endif

PLUGIN_FEATURES(
    PLUGIN_FEATURE_FILTER(TrafficFilter)
    );

PLUGIN_COMMANDS(
    {
        "traffic",
        TRUE, 0, 2,
        "Display the packet rate for each PID in the TS",
        "traffic [-s] [-i]\n"
        "Display the packet rate for each PID in the TS.\n"
        "Optionally, display known service association (-s) or information (-i).",
        CommandTraffic
    }
    );

PLUGIN_INTERFACE_CF(
    PLUGIN_FOR_ALL,
    "Traffic", "0.1", 
    "Plugin to display traffic on the current mux.", 
    "dvbstreamerplugin@nerdbox.net"
    );
/*******************************************************************************
* Filter Functions                                                             *
*******************************************************************************/
static void InitFilter(PIDFilter_t *filter)
{
    filter->name = "Traffic Capture";

    PIDFilterFilterPacketSet(filter, FilterPacket, NULL);   
    PIDFilterProcessPacketSet(filter, ProcessPacket, NULL);
    PIDFilterMultiplexChangeSet(filter, HandleMuxChange, NULL);

    pthread_mutex_lock(&TrafficMutex);

    PIDCounts = ListCreate();
    pidListLength = 0;
    serviceLock = FALSE;
    lastStart.tv_sec = currentStart.tv_sec = 0;
    lastStart.tv_usec = currentStart.tv_usec = 0;

    filter->enabled = TRUE;

    pthread_mutex_unlock(&TrafficMutex);
}

static void DeinitFilter(PIDFilter_t *filter)
{
    filter->enabled = FALSE;

    pthread_mutex_lock(&TrafficMutex);

    ListFree(PIDCounts, free);
    pidListLength = 0;
    PIDCounts = NULL;

    pthread_mutex_unlock(&TrafficMutex);
}

static void HandleMuxChange(struct PIDFilter_s *pidfilter, void *userarg, Multiplex_t *multiplex)
{
    pthread_mutex_lock(&TrafficMutex);

    ListFree(PIDCounts, free);
    PIDCounts = ListCreate();
    pidListLength = 0;
    serviceLock = FALSE;
    lastStart.tv_sec = currentStart.tv_sec = 0;
    lastStart.tv_usec = currentStart.tv_usec = 0;

    pthread_mutex_unlock(&TrafficMutex);
}

static int FilterPacket(PIDFilter_t *pidfilter, void *arg, uint16_t pid, TSPacket_t *packet)
{
    // We're only interested in collecting data when there's a lock on a mux
    if (serviceLock == FALSE)
    {
        DVBAdapter_t *adapter;
        Service_t * service;

        service = TuningCurrentServiceGet();
        if (!service)
        {
            return 0;
        }
        ServiceRefDec(service);
        adapter = MainDVBAdapterGet();
        if (adapter->frontEndLocked)
        {
            return 0;
        }

        serviceLock = TRUE;
    }

    return 1;
}

static TSPacket_t * ProcessPacket(PIDFilter_t *pidfilter, void *arg, TSPacket_t *packet)
{
    ListIterator_t iterator;
    TrafficPIDCount_t * data = NULL;
    uint16_t pid = TSPACKET_GETPID(*packet);
    
    if (PIDCounts != NULL) 
    {
        pthread_mutex_lock(&TrafficMutex);
        RotateData();
        for (ListIterator_Init(iterator, PIDCounts);
                ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
        {
            data = (TrafficPIDCount_t *)ListIterator_Current(iterator);
            if (data->PID >= pid)
            {
                break;
            }
        }
        if (data && data->PID == pid) 
        {
            data->count++;
        } 
        else 
        {
            data = calloc(1, sizeof(TrafficPIDCount_t));
            if (data)
            {
                data->PID = pid;
                data->count = 1;
                data->oldCount = 0;
                ListInsertBeforeCurrent(&iterator, data);
                pidListLength++;
            }
        }
        pthread_mutex_unlock(&TrafficMutex);
    }
    return NULL;
}

static void subtract_timeval(struct timeval a, struct timeval b,
    struct timeval * result) 
{
    result->tv_sec = a.tv_sec - b.tv_sec;
    result->tv_usec = a.tv_usec - b.tv_usec;
    if (result->tv_usec < 0) {
        result->tv_sec--;
        result->tv_usec += 1000000;
    }
}

static void RotateData()
{
    ListIterator_t iterator;
    TrafficPIDCount_t * data;
    struct timeval tmp;
    struct timeval now;

    if (serviceLock == FALSE)
    {
        return;
    }

    gettimeofday(&now, 0);
    if (currentStart.tv_sec == 0)
    {
        currentStart = now;
        lastStart = now;
    }

    subtract_timeval(now, currentStart, &tmp);
    if (tmp.tv_sec) 
    {
        lastStart = currentStart;
        currentStart = now;
        for (ListIterator_Init(iterator, PIDCounts);
                ListIterator_MoreEntries(iterator); )
        {
            data = (TrafficPIDCount_t *)ListIterator_Current(iterator);
            if (data->count == 0 && data->oldCount == 0)
            {
                ListRemoveCurrent(&iterator);
                pidListLength--;
            }
            else
            {
                data->oldCount = data->count;
                data->count = 0;
                ListIterator_Next(iterator);
            }
        }
    }
}

static TrafficPIDCount_t * CopyPIDCounts(void)
{
    ListIterator_t iterator;
    TrafficPIDCount_t * data;
    int i;

    data = calloc(pidListLength + 1, sizeof(TrafficPIDCount_t));
    if (data == NULL)
    {
        return NULL;
    }
    for (i = 0, ListIterator_Init(iterator, PIDCounts);
         i < pidListLength && ListIterator_MoreEntries(iterator); 
         i++, ListIterator_Next(iterator))
    {
        data[i] = *(TrafficPIDCount_t *)ListIterator_Current(iterator);
    }
    data[i].PID = PID_EOL;

    return data;
}

/*******************************************************************************
* Command Functions                                                            *
*******************************************************************************/
static void CommandTraffic(int argc, char **argv)
{
    bool printService = FALSE;
    bool printServiceName = FALSE;
    bool printServiceInfo = FALSE;
    TrafficPIDCount_t * data;
    Multiplex_t *multiplex = NULL;
    int i;
    uint64_t freq = 0;
    uint64_t rate;

    struct timeval tmp;
    uint64_t time;
    int timeout = 30; // Wait up to 6s, (30 * 200000 usec)

    for (i = 0; i < argc; i++)
    {
        if (strcmp(argv[i], "-s") == 0)
        {
            printService = TRUE;
            printServiceName = TRUE;
        }
        else if (strcmp(argv[i], "-i") == 0)
        {
            printService = TRUE;
            printServiceInfo = TRUE;
        }
        else
        {
            CommandPrintf("Invalid argument\n");
            return;
        }
    }

    /* Ensure the database is up-to-date */
    UpdateDatabase();
    
    /* Wait until there's data available */
    pthread_mutex_lock(&TrafficMutex);
    if (lastStart.tv_sec == currentStart.tv_sec 
            && lastStart.tv_usec == currentStart.tv_usec) 
    {
        RotateData();
        while (timeout > 0 && lastStart.tv_sec == currentStart.tv_sec
                && lastStart.tv_usec == currentStart.tv_usec)
        {
            pthread_mutex_unlock(&TrafficMutex);
            if (timeout == 28)
            {
                CommandPrintf("...Waiting up to 6 seconds for data to arrive...\n");
            }
            usleep(200000);
            pthread_mutex_lock(&TrafficMutex);
            RotateData();
            timeout--;
        }
    }
    if (serviceLock == FALSE)
    {
        pthread_mutex_unlock(&TrafficMutex);
        return;
    }

    /* Copy the data so that we don't printf on the critical path */
    data = CopyPIDCounts();
    pthread_mutex_unlock(&TrafficMutex);

    CommandPrintf(" PID          Frequency Datarate%s\n", 
            printService ? "   Service" : "");
    CommandPrintf("               (pkts/s) (kbit/s)\n");

    subtract_timeval(currentStart, lastStart, &tmp);
    time = tmp.tv_usec + tmp.tv_sec * 1000000;

    if (data == NULL)
    {
        printService = FALSE;
    }
    if (printService)
    {
        multiplex = TuningCurrentMultiplexGet();
    }
    if (!multiplex) {
        printService = FALSE;
    }

    for (i = 0; data[i].PID != PID_EOL; i++)
    {
        if (time != 0 && data[i].oldCount != 0)
        {
            freq = ((uint64_t)data[i].oldCount * 1000000)/time;
            rate = (freq * 188 * 8)/1024;
            if (printService)
            {
                char * name = "";
                char * info = "";
                Service_t *service;
                ServiceEnumerator_t enumerator;

                enumerator = ServiceFindByPID(data[i].PID, multiplex);
                service = ServiceGetNext(enumerator);
                if (service)
                {
                    if (printServiceName)
                    {
                        name = service->name;
                    }
                    if (printServiceInfo && data[i].PID == service->pmtPid)
                    {
                        info = " (PMT)";
                    }
                    if (printServiceInfo && data[i].PID == service->pcrPid)
                    {
                        info = " (PCR)";
                    }
                }
                CommandPrintf("%4d (0x%04x)     %5lld    %5lld - %s%s\n", 
                    data[i].PID, data[i].PID, freq, rate, name, info);
                ServiceRefDec(service);
                ServiceEnumeratorDestroy(enumerator);
            }
            else
            {
                CommandPrintf("%4d (0x%04x)     %5lld    %5lld\n", 
                    data[i].PID, data[i].PID, freq, rate);
            }
        }
    }

    free(data);

    if (printService)
    {
        MultiplexRefDec(multiplex);
    }
}

