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
#include "dvbpsi/datetime.h"
#include "dvbpsi/eit.h"
#include "dvbpsi/dr_4d.h"
#include "dvbpsi/dr_55.h"

#include "list.h"
#include "logging.h"
#include "subtableprocessor.h"



/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/
#define MAX_STRING_LEN 256

#define SHORT_EVENT_DR      0x4d
#define PARENTAL_RATINGS_DR 0x55

/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/

typedef struct Event_s
{
    uint16_t id;
    dvbpsi_date_time_t startTime;
    dvbpsi_eit_event_duration_t duration;
    List_t *shortEventDescriptors;
    dvbpsi_parental_rating_dr_t *parentalRatings;
}Event_t;

typedef struct ServiceSchedule_s
{
    uint16_t networkId;
    uint16_t tsId;
    uint16_t serviceId;
    List_t *events;
}ServiceSchedule_t;


/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void CommandDump(int argc, char **argv);
static void DumpChannels(void);
static void DumpProgrammes(void);

static void Init0x12Filter(PIDFilter_t *filter);
static void Deinit0x12Filter(PIDFilter_t *filter);
static void FreeServiceSchedule(void *ptr);
static void FreeEvent(void *ptr);

static void SubTableHandler(void * arg, dvbpsi_handle demuxHandle, uint8_t tableId, uint16_t extension);
static void ProcessEIT(void *arg, dvbpsi_eit_t *newEIT);

static void AddEvent(ServiceSchedule_t *serviceSchedule, dvbpsi_eit_event_t *event);
static bool EventPresent(ServiceSchedule_t *serviceSchedule, uint16_t id);
static ServiceSchedule_t *GetServiceSchedule(uint16_t networkId, uint16_t tsId, uint16_t serviceId);
static Service_t *FindService(uint16_t networkId, uint16_t tsId, uint16_t serviceId);
static void AddDuration(dvbpsi_date_time_t *datetime, dvbpsi_eit_event_duration_t *duration);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static PluginFilter_t filter = {NULL, Init0x12Filter, Deinit0x12Filter};
static const char EPGSCHEDULE[] = "EPGSchedule";
static List_t *servicesList = NULL;
/*******************************************************************************
* Plugin Setup                                                                 *
*******************************************************************************/
PLUGIN_COMMANDS(
    {
        "dumpepg",
        FALSE, 0, 0,
        "Dump EPG schedule.",
        "Dump the EPG schedule in XMLTV foramt.",
        CommandDump
    }
);

PLUGIN_FEATURES(
    PLUGIN_FEATURE_FILTER(filter)
    );

PLUGIN_INTERFACE_CF(
    "EPGSchedule", "0.1", 
    "Plugin to capture EPG schedule information.", 
    "charrea6@users.sourceforge.net"
    );

/*******************************************************************************
* Command Functions                                                            *
*******************************************************************************/
static void CommandDump(int argc, char **argv)
{
    CommandPrintf("<tv generator-info-name=\"DVBStreamer-EPGSchedule\">\n");
    DumpChannels();
    DumpProgrammes();
    CommandPrintf("</tv>\n");
}
static void DumpChannels(void)
{
    ListIterator_t iterator;

    for (ListIterator_Init(iterator, servicesList); 
         ListIterator_MoreEntries(iterator);
         ListIterator_Next(iterator))
     {
        ServiceSchedule_t *channel = (ServiceSchedule_t*)ListIterator_Current(iterator);
        Service_t *service = FindService(channel->networkId, channel->tsId, channel->serviceId);
        if (service)
        {
            CommandPrintf("<channel id=\"%s\">\n", service->name);
            CommandPrintf("<display-name>%s</display-name>\n", service->name);
            CommandPrintf("</channel>\n");
            ServiceRefDec(service);
        }
     }
}

static void DumpProgrammes(void)
{
    ListIterator_t srvIterator;

    for (ListIterator_Init(srvIterator, servicesList); 
         ListIterator_MoreEntries(srvIterator);
         ListIterator_Next(srvIterator))
     {
        ServiceSchedule_t *channel = (ServiceSchedule_t*)ListIterator_Current(srvIterator);
        ListIterator_t iterator;
        LogModule(LOG_DEBUG, EPGSCHEDULE, "Looking for service %04x:%04x:%04x\n", 
            channel->networkId, channel->tsId, channel->serviceId);

        Service_t *service = FindService(channel->networkId, channel->tsId, channel->serviceId);

        if (service)
        {
            LogModule(LOG_DEBUG, EPGSCHEDULE, "Found service %s\n", service->name);
            for (ListIterator_Init(iterator, channel->events); 
                 ListIterator_MoreEntries(iterator);
                 ListIterator_Next(iterator))
            {
                Event_t *event =(Event_t*)ListIterator_Current(iterator);
                ListIterator_t sedIterator;
                dvbpsi_date_time_t stopTime = event->startTime;
                AddDuration(&stopTime, &event->duration);

                CommandPrintf("<programme start=\"%04d%02d%02d%02d%02d%02d\" stop=\"%04d%02d%02d%02d%02d%02d\" channel=\"%s\">\n",
                    event->startTime.i_year, event->startTime.i_month, event->startTime.i_day,
                    event->startTime.i_hour, event->startTime.i_minute, event->startTime.i_second,
                    stopTime.i_year, stopTime.i_month, stopTime.i_day,
                    stopTime.i_hour, stopTime.i_minute, stopTime.i_second,                    
                    service->name);
                for (ListIterator_Init(sedIterator, event->shortEventDescriptors); 
                     ListIterator_MoreEntries(sedIterator);
                     ListIterator_Next(sedIterator))
                {
                    dvbpsi_short_event_dr_t *sed = (dvbpsi_short_event_dr_t *)ListIterator_Current(sedIterator);
                    char name[MAX_STRING_LEN];
                    memcpy(name, sed->i_event_name, sed->i_event_name_length);
                    name[sed->i_event_name_length] = 0;
                    CommandPrintf("<title lang=\"%c%c%c\">%s</title>\n", 
                        sed->i_iso_639_code[0],sed->i_iso_639_code[1],sed->i_iso_639_code[2],
                        name);
                }
                for (ListIterator_Init(sedIterator, event->shortEventDescriptors); 
                     ListIterator_MoreEntries(sedIterator);
                     ListIterator_Next(sedIterator))
                {
                    char description[MAX_STRING_LEN];
                    dvbpsi_short_event_dr_t *sed = (dvbpsi_short_event_dr_t *)ListIterator_Current(sedIterator);
                    memcpy(description, sed->i_text, sed->i_text_length);
                    description[sed->i_text_length] = 0;
                    CommandPrintf("<desc lang=\"%c%c%c\">%s</desc>\n", 
                        sed->i_iso_639_code[0],sed->i_iso_639_code[1],sed->i_iso_639_code[2],
                        description);
                }     
                    
                CommandPrintf("</programme>\n");
            }
        }
        ServiceRefDec(service);
     }
}
/*******************************************************************************
* Filter Functions                                                             *
*******************************************************************************/
static void Init0x12Filter(PIDFilter_t *filter)
{
    servicesList = ListCreate();
    ObjectRegisterType(ServiceSchedule_t);
    ObjectRegisterType(Event_t);
    filter->name = "EPG Schedule";
    filter->enabled = TRUE;
    SubTableProcessorInit(filter, 0x12, SubTableHandler, NULL, NULL, NULL);
}

static void Deinit0x12Filter(PIDFilter_t *filter)
{
    filter->enabled = FALSE;
    SubTableProcessorDeinit(filter);
    ListFree(servicesList, FreeServiceSchedule);
}

static void FreeServiceSchedule(void *ptr)
{
    ServiceSchedule_t *ss = (ServiceSchedule_t*)ptr;
    ListFree(ss->events,FreeEvent);
    ObjectRefDec(ss);
}

static void FreeEvent(void *ptr)
{
    Event_t *event = (Event_t*)ptr;
    ListFree(event->shortEventDescriptors, free);
    free(event->parentalRatings);
    ObjectRefDec(event);
}

static void SubTableHandler(void * arg, dvbpsi_handle demuxHandle, uint8_t tableId, uint16_t extension)
{
    if ((tableId >= 0x50) && (tableId <= 0x6f))
    {
        LogModule(LOG_DEBUG, EPGSCHEDULE, "Request for Sub-Table handler for %#02x (%#04x)\n", tableId, extension);

        dvbpsi_AttachEIT(demuxHandle, tableId, extension, ProcessEIT, NULL);
    }
}

static void ProcessEIT(void *arg, dvbpsi_eit_t *newEIT)
{
    dvbpsi_eit_event_t *event;
    LogModule(LOG_DEBUG, EPGSCHEDULE, "EIT received (version %d) net id %x ts id %x service id %x\n",
        newEIT->i_version, newEIT->i_network_id, newEIT->i_ts_id, newEIT->i_service_id);
    ServiceSchedule_t *ss = GetServiceSchedule( newEIT->i_network_id, newEIT->i_ts_id, newEIT->i_service_id);
    
    for (event = newEIT->p_first_event; event; event = event->p_next)
    {
        if (!EventPresent(ss,event->i_event_id))
        {
            AddEvent(ss,event);
        }
    }

    dvbpsi_DeleteEIT(newEIT);
}


/*******************************************************************************
* Helper Functions                                                             *
*******************************************************************************/
static void AddEvent(ServiceSchedule_t *serviceSchedule, dvbpsi_eit_event_t *eitevent)
{
    Event_t *newEvent = ObjectCreateType(Event_t);
    dvbpsi_descriptor_t *descriptor;
    newEvent->shortEventDescriptors = ListCreate();
    newEvent->id = eitevent->i_event_id;
    newEvent->startTime = eitevent->t_start_time;
    newEvent->duration = eitevent->t_duration;
    for (descriptor = eitevent->p_first_descriptor; descriptor; descriptor = descriptor->p_next)
    {
        if (descriptor->i_tag == SHORT_EVENT_DR)
        {
            dvbpsi_short_event_dr_t * sed = dvbpsi_DecodeShortEventDr(descriptor);
            descriptor->p_decoded = NULL; /* Unlink the decode descriptor so it don't get free'd */
            ListAdd(newEvent->shortEventDescriptors, sed);
        }
        if ((descriptor->i_tag == PARENTAL_RATINGS_DR) && !newEvent->parentalRatings)
        {
            dvbpsi_parental_rating_dr_t * prd = dvbpsi_DecodeParentalRatingDr(descriptor);
            descriptor->p_decoded = NULL; /* Unlink the decode descriptor so it don't get free'd */
            newEvent->parentalRatings = prd;
        }
    }
    ListAdd(serviceSchedule->events, newEvent);
}

static bool EventPresent(ServiceSchedule_t *serviceSchedule, uint16_t id)
{
    bool found = FALSE;
    ListIterator_t iterator;

    for (ListIterator_Init(iterator,serviceSchedule->events);
         ListIterator_MoreEntries(iterator);
         ListIterator_Next(iterator))
    {
        Event_t *event = (Event_t*)ListIterator_Current(iterator);
        if (event->id == id)
        {
            found = TRUE;
            break;
        }
    }
    return found;
}

static ServiceSchedule_t *GetServiceSchedule(uint16_t networkId, uint16_t tsId, uint16_t serviceId)
{
    ServiceSchedule_t * result = NULL;
    ListIterator_t iterator;

    for (ListIterator_Init(iterator,servicesList);
         ListIterator_MoreEntries(iterator);
         ListIterator_Next(iterator))
    {
        ServiceSchedule_t *ss = (ServiceSchedule_t *)ListIterator_Current(iterator);
        if ((ss->networkId == networkId) && (ss->tsId == tsId) && (ss->serviceId == serviceId))
        {
            result = ss;
            break;
        }
    }
    if (!result)
    {
        result = ObjectCreateType(ServiceSchedule_t);
        result->networkId = networkId;
        result->tsId = tsId;
        result->serviceId = serviceId;
        result->events = ListCreate();
        ListAdd(servicesList,result);
    }
    return result;
}

static Service_t *FindService(uint16_t networkId, uint16_t tsId, uint16_t serviceId)
{
    Service_t *service = NULL;
    Multiplex_t *multiplex;

    multiplex = MultiplexFindId(networkId, tsId);
    if (multiplex)
    {
        LogModule(LOG_DEBUG, EPGSCHEDULE, "Found multiplex uid %d looking for service %d\n", multiplex->uid, serviceId); 
        service = ServiceFindId(multiplex, serviceId);
        MultiplexRefDec(multiplex);
    }
    return service;
}

static void AddDuration(dvbpsi_date_time_t *datetime, dvbpsi_eit_event_duration_t *duration)
{
    struct tm tm_time;
    struct tm *new_time;
    time_t secs;
    
    tm_time.tm_year = datetime->i_year;
    tm_time.tm_mon  = datetime->i_month;
    tm_time.tm_mday = datetime->i_day;
    tm_time.tm_hour = datetime->i_hour;
    tm_time.tm_min  = datetime->i_minute;
    tm_time.tm_sec  = datetime->i_second;

    secs = mktime(&tm_time);

    secs += (datetime->i_hour * 60 * 60) + (datetime->i_minute * 60) + datetime->i_second;
    new_time = gmtime(&secs);

    datetime->i_year   = new_time->tm_year;
    datetime->i_month  = new_time->tm_mon;
    datetime->i_day    = new_time->tm_mday;
    datetime->i_hour   = new_time->tm_hour;
    datetime->i_minute = new_time->tm_min;
    datetime->i_second = new_time->tm_sec;
}
