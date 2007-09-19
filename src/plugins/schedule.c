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

schedule.c

Plugin to collect EPG schedule information.

*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>

#include "plugin.h"
#include "epgdbase.h"
#include "dvbpsi/datetime.h"
#include "dvbpsi/eit.h"
#include "dvbpsi/dr_4d.h"
#include "dvbpsi/dr_55.h"

#include "list.h"
#include "logging.h"
#include "subtableprocessor.h"
#include "dvbtext.h"


/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/
#define EIT_PID 0x12
#define MAX_STRING_LEN 256

#define SHORT_EVENT_DR      0x4d
#define PARENTAL_RATINGS_DR 0x55

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void Init0x12Filter(PIDFilter_t *filter);
static void Deinit0x12Filter(PIDFilter_t *filter);

static void FreeEIT(void *arg);
static void EnqueueEIT(dvbpsi_eit_t *newEIT);
static dvbpsi_eit_t * DequeueEIT(void);

static void SubTableHandler(void * arg, dvbpsi_handle demuxHandle, uint8_t tableId, uint16_t extension);
static void ProcessEIT(void *arg, dvbpsi_eit_t *newEIT);

static void *EITProcessor(void *arg);

static void ProcessEvent(EPGServiceRef_t *serviceRef, dvbpsi_eit_event_t *event);

static void ConvertToTM(dvbpsi_date_time_t *datetime, dvbpsi_eit_event_duration_t *duration,
    struct tm *startTime, struct tm *endTime);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static PluginFilter_t filter = {NULL, Init0x12Filter, Deinit0x12Filter};
static const char DVBSCHEDULE[] = "DVBSchedule";

static List_t *eitQueue = NULL;
static pthread_mutex_t eitQueueMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t eitAvailableCond = PTHREAD_COND_INITIALIZER;
static pthread_t eitProcessorThread;
static bool eitProcessorExit = FALSE;
static char *RatingsTable[] = {
    "Undefined",
    "4",
    "5",    
    "6",
    "7",
    "8",
    "9",
    "10",
    "11",
    "12",
    "13",
    "14",
    "15",
    "16",
    "17",
    "18"
};
/*******************************************************************************
* Plugin Setup                                                                 *
*******************************************************************************/
#ifdef __CYGWIN__
#define PluginInterface DVBSchedulePluginInterface
#endif

PLUGIN_FEATURES(
    PLUGIN_FEATURE_FILTER(filter)
    );

PLUGIN_INTERFACE_F(
    PLUGIN_FOR_DVB,
    "DVBSchedule", "0.1", 
    "Plugin to capture DVB EPG schedule information.", 
    "charrea6@users.sourceforge.net"
    );
/*******************************************************************************
* Filter Functions                                                             *
*******************************************************************************/
static void Init0x12Filter(PIDFilter_t *filter)
{
    eitQueue = ListCreate();

    filter->name = "DVB Schedule";
    filter->enabled = TRUE;
    SubTableProcessorInit(filter, 0x12, SubTableHandler, NULL, NULL, NULL);
    pthread_create(&eitProcessorThread, NULL, EITProcessor, NULL);
    if (filter->tsFilter->adapter->hardwareRestricted)
    {
        DVBDemuxAllocateFilter(filter->tsFilter->adapter, EIT_PID, TRUE);
    }      
}

static void Deinit0x12Filter(PIDFilter_t *filter)
{
    filter->enabled = FALSE;
    if (filter->tsFilter->adapter->hardwareRestricted)
    {
        DVBDemuxReleaseFilter(filter->tsFilter->adapter, EIT_PID);
    }                
    SubTableProcessorDeinit(filter);

    pthread_mutex_lock(&eitQueueMutex);
    eitProcessorExit = TRUE;
    pthread_cond_signal(&eitAvailableCond);
    pthread_mutex_unlock(&eitQueueMutex); 

    pthread_join(eitProcessorThread, NULL);
    
    ListFree(eitQueue, FreeEIT );
}

static void FreeEIT(void *arg)
{
    dvbpsi_eit_t *eit = arg;
    dvbpsi_DeleteEIT(eit);
}

static void EnqueueEIT(dvbpsi_eit_t *newEIT)
{
    pthread_mutex_lock(&eitQueueMutex);
    ListAdd(eitQueue, newEIT);
    pthread_cond_signal(&eitAvailableCond);
    pthread_mutex_unlock(&eitQueueMutex);    
}

static dvbpsi_eit_t * DequeueEIT(void)
{
    dvbpsi_eit_t *eit = NULL;
    pthread_mutex_lock(&eitQueueMutex);
    if (ListCount(eitQueue) == 0)
    {
        pthread_cond_wait(&eitAvailableCond, &eitQueueMutex);
    }

    if (!eitProcessorExit)
    {
        ListIterator_t iterator;
        ListIterator_Init(iterator, eitQueue);
        eit = ListIterator_Current(iterator);
        ListRemoveCurrent(&iterator);
    }

    pthread_mutex_unlock(&eitQueueMutex);  
    return eit;
}

static void SubTableHandler(void * arg, dvbpsi_handle demuxHandle, uint8_t tableId, uint16_t extension)
{
    if ((tableId >= 0x50) && (tableId <= 0x6f))
    {
        LogModule(LOG_DEBUG, DVBSCHEDULE, "Request for Sub-Table handler for %#02x (%#04x)\n", tableId, extension);

        dvbpsi_AttachEIT(demuxHandle, tableId, extension, ProcessEIT, NULL);
    }
}

static void ProcessEIT(void *arg, dvbpsi_eit_t *newEIT)
{
    LogModule(LOG_DEBUG, DVBSCHEDULE, "EIT received (version %d) net id %x ts id %x service id %x\n",
        newEIT->i_version, newEIT->i_network_id, newEIT->i_ts_id, newEIT->i_service_id);
    EnqueueEIT( newEIT);
}

static void *EITProcessor(void *arg)
{
    while (!eitProcessorExit)
    {
        dvbpsi_eit_t *eit = DequeueEIT();
        if (eit)
        {
            EPGServiceRef_t serviceRef;
            dvbpsi_eit_event_t *event;

            LogModule(LOG_DEBUG, DVBSCHEDULE, "Processing EIT (version %d) net id %x ts id %x service id %x\n",
            eit->i_version, eit->i_network_id, eit->i_ts_id, eit->i_service_id);
            
            EPGDBaseTransactionStart();
            serviceRef.netId = eit->i_network_id;
            serviceRef.tsId = eit->i_ts_id;
            serviceRef.serviceId = eit->i_service_id;
            for (event = eit->p_first_event; event; event = event->p_next)
            {
                ProcessEvent(&serviceRef, event);
            }
            dvbpsi_DeleteEIT(eit);
            EPGDBaseTransactionCommit();
        }
    }
    LogModule(LOG_DEBUG, DVBSCHEDULE, "EIT Processor thread exiting.\n");
    return NULL;
}

/*******************************************************************************
* Helper Functions                                                             *
*******************************************************************************/
static void ProcessEvent(EPGServiceRef_t *serviceRef, dvbpsi_eit_event_t *eitevent)
{
    EPGEvent_t epgevent;
    dvbpsi_descriptor_t *descriptor;
    char startTimeStr[25];
    char endTimeStr[25];
    epgevent.serviceRef = *serviceRef;
    epgevent.eventId = eitevent->i_event_id;
    ConvertToTM(&eitevent->t_start_time, &eitevent->t_duration, &epgevent.startTime, &epgevent.endTime);
    epgevent.ca = eitevent->b_free_ca;
    strftime(startTimeStr, sizeof(startTimeStr), "%Y-%m-%d %T", &epgevent.startTime);
    strftime(endTimeStr, sizeof(startTimeStr), "%Y-%m-%d %T", &epgevent.endTime);
    LogModule(LOG_DEBUG, DVBSCHEDULE, "(%x:%x:%x) Event %x Start Time %s End Time %s\n",
        serviceRef->netId, serviceRef->tsId, serviceRef->serviceId, epgevent.eventId,
        startTimeStr, endTimeStr);
    
    if (EPGDBaseEventAdd(&epgevent) != 0)
    {
        return;
    }
        
    for (descriptor = eitevent->p_first_descriptor; descriptor; descriptor = descriptor->p_next)
    {
        if (descriptor->i_tag == SHORT_EVENT_DR)
        {
            char lang[4];
            char *temp;
            dvbpsi_short_event_dr_t * sed = dvbpsi_DecodeShortEventDr(descriptor);
            lang[0] = sed->i_iso_639_code[0];
            lang[1] = sed->i_iso_639_code[1];
            lang[2] = sed->i_iso_639_code[2];
            lang[3] = 0;
            temp = DVBTextToUTF8((char *)sed->i_event_name, sed->i_event_name_length);
            EPGDBaseDetailAdd(serviceRef, epgevent.eventId, lang, EPG_EVENT_DETAIL_TITLE, temp);
            temp = DVBTextToUTF8((char *)sed->i_text, sed->i_text_length);
            EPGDBaseDetailAdd(serviceRef, epgevent.eventId, lang, EPG_EVENT_DETAIL_DESCRIPTION, temp);
        }
        if (descriptor->i_tag == PARENTAL_RATINGS_DR)
        {
            dvbpsi_parental_rating_dr_t * prd = dvbpsi_DecodeParentalRatingDr(descriptor);
            char cc[4];
            int i;
            cc[3] = 0;
            
            for (i=0; i < prd->i_ratings_number; i ++)
            {
                cc[0] = prd->p_parental_rating[i].i_country_code >> 16;
                cc[1] = prd->p_parental_rating[i].i_country_code >> 8;
                cc[2] = prd->p_parental_rating[i].i_country_code >> 0;
                if (prd->p_parental_rating[i].i_rating < 0x0f)
                {
                    char *rating =  RatingsTable[prd->p_parental_rating[i].i_rating];
                    EPGDBaseRatingAdd(serviceRef, epgevent.eventId, cc,rating);
                }
            }
        }
    }
}

static void ConvertToTM(dvbpsi_date_time_t *datetime, dvbpsi_eit_event_duration_t *duration,
    struct tm *startTime, struct tm *endTime)
{
    struct tm *temp_time;
    time_t secs;

    startTime->tm_year = datetime->i_year - 1900;
    startTime->tm_mon  = datetime->i_month - 1;
    startTime->tm_mday = datetime->i_day;
    startTime->tm_hour = datetime->i_hour;
    startTime->tm_min  = datetime->i_minute;
    startTime->tm_sec  = datetime->i_second;

    secs = mktime(startTime);
    
    secs += (duration->i_hours * 60 * 60) + (duration->i_minutes* 60) + duration->i_seconds;

    temp_time = gmtime(&secs);
    *endTime = *temp_time;
}
