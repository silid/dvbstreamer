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
#include "main.h"
#include "epgchannel.h"
#include "dvbpsi/datetime.h"
#include "dvbpsi/eit.h"
#include "dvbpsi/dr_4d.h"
#include "dvbpsi/dr_55.h"
#include "dvbpsi/dr_76.h"

#include "list.h"
#include "logging.h"
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
static void Install(bool installed);
static void SubTableHandler(void * arg, dvbpsi_handle demuxHandle, uint8_t tableId, uint16_t extension);
static void ProcessEIT(void *arg, dvbpsi_eit_t *newEIT);
static void DeferredProcessEIT(void *arg);

static void CommandEPGCapRestart(int argc, char **argv);
static void CommandEPGCapStart(int argc, char **argv);
static void CommandEPGCapStop(int argc, char **argv);

static void ProcessEvent(EPGServiceRef_t *serviceRef, dvbpsi_eit_event_t *event);

static void ConvertToTM(struct tm *startTime, uint32_t duration, struct tm *endTime);

static char *ResolveCRID(EPGServiceRef_t *serviceRef, char *relativeCRID);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static const char DVBTOEPG[] = "DVBTOEPG";

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
PLUGIN_FEATURES(
    PLUGIN_FEATURE_INSTALL(Install)
    );

PLUGIN_COMMANDS({
        "epgcaprestart",
        0, 0,
        "Starts or restarts the capturing of EPG content.",
        "Starts or restarts the capturing of EPG content, for use by EPG capture applications.",
        CommandEPGCapRestart
    },
    {
        "epgcapstart",
        0, 0,
        "Starts the capturing of EPG content.",
        "Starts the capturing of EPG content, for use by EPG capture applications.",
        CommandEPGCapStart
    },
    {
        "epgcapstop",
        0, 0,
        "Stops the capturing of EPG content.",
        "Stops the capturing of EPG content, for use by EPG capture applications.",
        CommandEPGCapStop
    }
);
    
PLUGIN_INTERFACE_CF(
    PLUGIN_FOR_DVB,
    "DVBTOEPG", "0.3",
    "Plugin to capture DVB EPG schedule information.",
    "charrea6@users.sourceforge.net"
    );
/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/

static TSFilterGroup_t *tsgroup;
static dvbpsi_handle demux = NULL;

/*******************************************************************************
* Filter Functions                                                             *
*******************************************************************************/
static void Install(bool installed)
{
    if (!installed)
    {
        if (tsgroup)
        {
            TSFilterGroupDestroy(tsgroup);
            dvbpsi_DetachDemux(demux);
        }
    }
}

static void DVBtoEPGFilterGroupEventCallback(void *arg, TSFilterGroup_t *group, TSFilterEventType_e event, void *details)
{
    if (event == TSFilterEventType_MuxChanged)
    {
        TSFilterGroupRemoveSectionFilter(tsgroup, PID_EIT);
        dvbpsi_DetachDemux(demux);
        demux = dvbpsi_AttachDemux(SubTableHandler, NULL);
        TSFilterGroupAddSectionFilter(tsgroup, PID_EIT, 3, demux);
    }
}

static void SubTableHandler(void * arg, dvbpsi_handle demuxHandle, uint8_t tableId, uint16_t extension)
{
    if ((tableId >= 0x50) && (tableId <= 0x6f))
    {
        LogModule(LOG_DEBUG, DVBTOEPG, "Request for Sub-Table handler for %#02x (%#04x)\n", tableId, extension);

        dvbpsi_AttachEIT(demuxHandle, tableId, extension, ProcessEIT, NULL);
    }
}

static void ProcessEIT(void *arg, dvbpsi_eit_t *newEIT)
{
    LogModule(LOG_DEBUG, DVBTOEPG, "EIT received (version %d) net id %x ts id %x service id %x\n",
        newEIT->i_version, newEIT->i_network_id, newEIT->i_ts_id, newEIT->i_service_id);
    DeferredProcessingAddJob(DeferredProcessEIT, newEIT );
    ObjectRefDec(newEIT);
}

static void DeferredProcessEIT(void *arg)
{
    dvbpsi_eit_t *eit = (dvbpsi_eit_t *)arg;
    EPGServiceRef_t serviceRef;
    dvbpsi_eit_event_t *event;

    LogModule(LOG_DEBUG, DVBTOEPG, "Processing EIT (version %d) net id %x ts id %x service id %x\n",
    eit->i_version, eit->i_network_id, eit->i_ts_id, eit->i_service_id);

    serviceRef.netId = eit->i_network_id;
    serviceRef.tsId = eit->i_ts_id;
    serviceRef.serviceId = eit->i_service_id;
    for (event = eit->p_first_event; event; event = event->p_next)
    {
        ProcessEvent(&serviceRef, event);
    }
    ObjectRefDec(eit);
}

/*******************************************************************************
* Command Functions                                                            *
*******************************************************************************/
static void CommandEPGCapRestart(int argc, char **argv)
{
    if (tsgroup)
    {
        DVBtoEPGFilterGroupEventCallback(NULL, tsgroup, TSFilterEventType_MuxChanged, NULL);
    }
}

static void CommandEPGCapStart(int argc, char **argv)
{
    if (tsgroup)
    {
        CommandError(COMMAND_ERROR_GENERIC, "Already started!");
        return;
    }
    tsgroup = TSReaderCreateFilterGroup(MainTSReaderGet(), DVBTOEPG, "DVB", DVBtoEPGFilterGroupEventCallback, NULL);
    demux = dvbpsi_AttachDemux(SubTableHandler, NULL);
    TSFilterGroupAddSectionFilter(tsgroup, PID_EIT, 3, demux);
    
}

static void CommandEPGCapStop(int argc, char **argv)
{
    if (tsgroup)
    {
        CommandError(COMMAND_ERROR_GENERIC, "Not yet started!");
        return;
    }
    TSFilterGroupDestroy(tsgroup);
    dvbpsi_DetachDemux(demux);
    tsgroup = NULL;
}

/*******************************************************************************
* Helper Functions                                                             *
*******************************************************************************/
static void ProcessEvent(EPGServiceRef_t *serviceRef, dvbpsi_eit_event_t *eitevent)
{
    EPGEventRef_t eventRef;
    dvbpsi_descriptor_t *descriptor;
    struct tm endTime;
    char startTimeStr[25];
    char endTimeStr[25];
    
    eventRef.serviceRef = *serviceRef;
    eventRef.eventId = eitevent->i_event_id;
    
    ConvertToTM(&eitevent->t_start_time, eitevent->i_duration, &endTime);
    if (LogLevelIsEnabled(LOG_DEBUG))
    {
        strftime(startTimeStr, sizeof(startTimeStr), "%Y-%m-%d %T", &eitevent->t_start_time);
        strftime(endTimeStr, sizeof(startTimeStr), "%Y-%m-%d %T", &endTime);
        LogModule(LOG_DEBUG, DVBTOEPG, "(%x:%x:%x) Event %x Start Time %s End Time %s\n",
            eventRef.serviceRef.netId, eventRef.serviceRef.tsId, eventRef.serviceRef.serviceId, eventRef.eventId,
            startTimeStr, endTimeStr);
    }
    if (EPGChannelNewEvent(&eventRef, &eitevent->t_start_time, &endTime, eitevent->b_free_ca) != 0)
    {
        LogModule(LOG_DEBUG, DVBTOEPG, "Failed to send returning...");
        return;
    }

    for (descriptor = eitevent->p_first_descriptor; descriptor; descriptor = descriptor->p_next)
    {
        LogModule(LOG_DEBUG, DVBTOEPG, "Tag %02x", descriptor->i_tag);
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
                        EPGChannelNewDetail(&eventRef, lang, EPG_EVENT_DETAIL_TITLE, temp);
                        free(temp);
                    }
                    temp = DVBTextToUTF8((char *)sed->i_text, sed->i_text_length);
                    if (temp)
                    {
                        EPGChannelNewDetail(&eventRef, lang, EPG_EVENT_DETAIL_DESCRIPTION, temp);
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
                            EPGChannelNewRating(&eventRef, cc, rating);
                        }
                    }
                }
                break;
            case CRID_DR:
                {
                    dvbpsi_content_id_dr_t * cridd = dvbpsi_DecodeContentIdDr(descriptor);
                    int i;
                    char *type = NULL;
                    LogModule(LOG_DEBUG, DVBTOEPG, "CRID Descriptor with %d entries\n", cridd->i_number_of_entries);
                    for (i = 0;i < cridd->i_number_of_entries; i ++)
                    {
                        LogModule(LOG_DEBUG, DVBTOEPG, "%d) Type    : %d\n", i, cridd->p_entries[i].i_type);
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
                        LogModule(LOG_DEBUG, DVBTOEPG, "%d) Location: %d\n", i, cridd->p_entries[i].i_location);

                        if (cridd->p_entries[i].i_location == CRID_LOCATION_DESCRIPTOR)
                        {
                            LogModule(LOG_DEBUG, DVBTOEPG, "%d) Path    : %s\n", i, cridd->p_entries[i].value.path);
                            if (type)
                            {
                                if (cridd->p_entries[i].value.path[0] == '/')
                                {
                                    char *crid = ResolveCRID(serviceRef, (char*)cridd->p_entries[i].value.path);
                                    if (crid)
                                    {
                                        EPGChannelNewDetail(&eventRef, (char*)ISO639NoLinguisticContent,
                                        type, crid);
                                        free(crid);
                                    }

                                }
                                else
                                {
                                    EPGChannelNewDetail(&eventRef, (char*)ISO639NoLinguisticContent,
                                        type, (char *)cridd->p_entries[i].value.path);
                                }
                            }
                        }
                        else
                        {
                            LogModule(LOG_DEBUG, DVBTOEPG, "%d) Ref     : %d\n", i, cridd->p_entries[i].value.ref);
                        }
                    }

                }
                break;
        }

    }
    LogModule(LOG_DEBUG, DVBTOEPG, "(%x:%x:%x) Event %x Finished\n",
        eventRef.serviceRef.netId, eventRef.serviceRef.tsId, eventRef.serviceRef.serviceId, eventRef.eventId);    
}

static void ConvertToTM(struct tm *startTime, uint32_t duration, struct tm *endTime)
{
    struct tm *temp_time;
    time_t secs;

    secs = timegm(startTime);

    secs += duration;

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
                LogModule(LOG_INFO, DVBTOEPG, "Failed to allocate memory for resolved CRID string.");
            }
        }
        ServiceRefDec(service);
    }

    return result;
}
