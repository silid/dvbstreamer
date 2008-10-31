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

#include "plugin.h"
#include "epgdbase.h"
#include "dvbpsi/datetime.h"
#include "dvbpsi/eit.h"
#include "dvbpsi/dr_4d.h"
#include "dvbpsi/dr_55.h"
#include "dvbpsi/dr_76.h"

#include "list.h"
#include "logging.h"
#include "subtableprocessor.h"
#include "dvbtext.h"
#include "deferredproc.h"


/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/
#define EIT_PID 0x12
#define MAX_STRING_LEN 256

#define SHORT_EVENT_DR      0x4d
#define PARENTAL_RATINGS_DR 0x55
#define CRID_DR             0x76

#define UK_FREEVIEW_CONTENT 49
#define UK_FREEVIEW_SERIES  50

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void Init0x12Filter(PIDFilter_t *filter);
static void Deinit0x12Filter(PIDFilter_t *filter);
static void SubTableHandler(void * arg, dvbpsi_handle demuxHandle, uint8_t tableId, uint16_t extension);
static void ProcessEIT(void *arg, dvbpsi_eit_t *newEIT);
static void DeferredProcessEIT(void *arg);

static void ProcessEvent(EPGServiceRef_t *serviceRef, dvbpsi_eit_event_t *event);

static void ConvertToTM(dvbpsi_date_time_t *datetime, dvbpsi_eit_event_duration_t *duration,
    struct tm *startTime, struct tm *endTime);

static char *ResolveCRID(EPGServiceRef_t *serviceRef, char *relativeCRID);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static PluginFilter_t filter = {NULL, Init0x12Filter, Deinit0x12Filter};
static const char DVBSCHEDULE[] = "DVBSchedule";

static const char ISO639NoLinguisticContent[] = "zxx";

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
    "DVBSchedule", "0.2",
    "Plugin to capture DVB EPG schedule information.",
    "charrea6@users.sourceforge.net"
    );
/*******************************************************************************
* Filter Functions                                                             *
*******************************************************************************/
static void Init0x12Filter(PIDFilter_t *filter)
{
    filter->name = "DVB Schedule";
    filter->enabled = TRUE;

    SubTableProcessorInit(filter, 0x12, SubTableHandler, NULL, NULL, NULL);
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
    DeferredProcessingAddJob(DeferredProcessEIT, newEIT );
    ObjectRefDec(newEIT);
}

static void DeferredProcessEIT(void *arg)
{
    dvbpsi_eit_t *eit = (dvbpsi_eit_t *)arg;
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
    ObjectRefDec(eit);
    EPGDBaseTransactionCommit();
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
        switch(descriptor->i_tag)
        {
            case SHORT_EVENT_DR:
                {
                    char lang[4];
                    char *temp;
                    dvbpsi_short_event_dr_t * sed = dvbpsi_DecodeShortEventDr(descriptor);
                    lang[0] = sed->i_iso_639_code[0];
                    lang[1] = sed->i_iso_639_code[1];
                    lang[2] = sed->i_iso_639_code[2];
                    lang[3] = 0;
                    temp = DVBTextToUTF8((char *)sed->i_event_name, sed->i_event_name_length);
                    if (temp)
                    {
                        EPGDBaseDetailAdd(serviceRef, epgevent.eventId, lang, EPG_EVENT_DETAIL_TITLE, temp);
                        free(temp);
                    }
                    temp = DVBTextToUTF8((char *)sed->i_text, sed->i_text_length);
                    if (temp)
                    {
                        EPGDBaseDetailAdd(serviceRef, epgevent.eventId, lang, EPG_EVENT_DETAIL_DESCRIPTION, temp);
                        free(temp);
                    }
                }
                break;
            case PARENTAL_RATINGS_DR:
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
                break;
            case CRID_DR:
                {
                    dvbpsi_content_id_dr_t * cridd = dvbpsi_DecodeContentIdDr(descriptor);
                    int i;
                    char *type = NULL;
                    LogModule(LOG_DEBUG, DVBSCHEDULE, "CRID Descriptor with %d entries\n", cridd->i_number_of_entries);
                    for (i = 0;i < cridd->i_number_of_entries; i ++)
                    {
                        LogModule(LOG_DEBUG, DVBSCHEDULE, "%d) Type    : %d\n", i, cridd->p_entries[i].i_type);
                        switch (cridd->p_entries[i].i_type)
                        {
                            case UK_FREEVIEW_CONTENT:
                            case CRID_TYPE_CONTENT:
                                type = "content";
                                break;
                            case UK_FREEVIEW_SERIES:
                            case CRID_TYPE_SERIES:
                                type = "series";
                                break;
                            default:
                                type = NULL;
                                break;
                        }
                        LogModule(LOG_DEBUG, DVBSCHEDULE, "%d) Location: %d\n", i, cridd->p_entries[i].i_location);

                        if (cridd->p_entries[i].i_location == CRID_LOCATION_DESCRIPTOR)
                        {
                            LogModule(LOG_DEBUG, DVBSCHEDULE, "%d) Path    : %s\n", i, cridd->p_entries[i].value.path);
                            if (type)
                            {
                                if (cridd->p_entries[i].value.path[0] == '/')
                                {
                                    char *crid = ResolveCRID(serviceRef, (char*)cridd->p_entries[i].value.path);
                                    if (crid)
                                    {
                                        EPGDBaseDetailAdd(serviceRef, epgevent.eventId, (char*)ISO639NoLinguisticContent,
                                        type, crid);
                                        free(crid);
                                    }

                                }
                                else
                                {
                                    EPGDBaseDetailAdd(serviceRef, epgevent.eventId, (char*)ISO639NoLinguisticContent,
                                        type, (char *)cridd->p_entries[i].value.path);
                                }
                            }
                        }
                        else
                        {
                            LogModule(LOG_DEBUG, DVBSCHEDULE, "%d) Ref     : %d\n", i, cridd->p_entries[i].value.ref);
                        }
                    }

                }
                break;
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

static char *ResolveCRID(EPGServiceRef_t *serviceRef, char *relativeCRID)
{
    char *result = NULL;
    Service_t *service;
    service = ServiceFindFQID(serviceRef->netId, serviceRef->tsId, serviceRef->serviceId);
    if (service)
    {
        if (service->defaultAuthority)
        {
            if (asprintf(&result, "%s%s", service->defaultAuthority,relativeCRID) == -1)
            {
                LogModule(LOG_INFO, DVBSCHEDULE, "Failed to allocate memory for resolved CRID string.");
            }
        }
        ServiceRefDec(service);
    }

    return result;
}
