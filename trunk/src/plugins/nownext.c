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

nownext.c

Plugin to display Present/Following (ie Now/Next) EPG information.

*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdint.h>

#include "plugin.h"
#include <dvbpsi/eit.h>
#include <dvbpsi/dr_4d.h>
#include "list.h"
#include "logging.h"
#include "subtableprocessor.h"
#include "dvbpsi/tdttot.h"


/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/
#define TABLE_ID_PF_ACTUAL 0x4e
#define TABLE_ID_PF_OTHER  0x4f

#define MAX_STRING_LEN 256


/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/
struct DVBTime_t
{
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
};

struct DVBDuration_t
{
    int hour;
    int minute;
    int second;    
};

typedef struct Event_s
{
    char name[MAX_STRING_LEN];
    char description[MAX_STRING_LEN];
    struct DVBTime_t startTime;
    struct DVBDuration_t duration;
}Event_t;

typedef struct ServiceNowNextInfo_s
{
    uint16_t networkId;
    uint16_t tsId;
    uint16_t serviceId;
    Event_t  now;
    Event_t  next;
}ServiceNowNextInfo_t;


/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void CommandNow(int argc, char **argv);
static void CommandNext(int argc, char **argv);
static void PrintEvent(Event_t *event);
static void Init0x12Filter(PIDFilter_t *filter);
static void Deinit0x12Filter(PIDFilter_t *filter);
static ServiceNowNextInfo_t *FindServiceName(char *name);
static ServiceNowNextInfo_t *FindService(uint16_t networkId, uint16_t tsId, uint16_t serviceId);
static void SubTableHandler(void * arg, dvbpsi_handle demuxHandle, uint8_t tableId, uint16_t extension);
static void ProcessEIT(void *arg, dvbpsi_eit_t *newEIT);
static void UpdateEvent(Event_t *event, dvbpsi_eit_event_t *eitevent);


/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static PluginFilter_t filter = {NULL, Init0x12Filter, Deinit0x12Filter};
static List_t *serviceNowNextInfoList;


/*******************************************************************************
* Plugin Setup                                                                 *
*******************************************************************************/
PLUGIN_COMMANDS(
    {
        "now",
        FALSE, 1, 1,
        "Display the current program.",
        "Display the current program on all channels (assuming the data is "
        "present on the current TS).",
        CommandNow
    },
    {
        "next",
        FALSE, 1, 1,
        "Display the next program.",
        "Display the next program on all channels (assuming the data is "
        "present on the current TS).",
        CommandNext
    }
);

PLUGIN_FEATURES(
    PLUGIN_FEATURE_FILTER(filter)
    );

PLUGIN_INTERFACE_CF(
    "NowNext", "0.1", 
    "Plugin to display present/following EPG information.", 
    "charrea6@users.sourceforge.net"
    );

/*******************************************************************************
* Command Functions                                                            *
*******************************************************************************/
static void CommandNow(int argc, char **argv)
{
    ServiceNowNextInfo_t *info = FindServiceName(argv[0]);
    if (info)
    {
        PrintEvent(&info->now);
    }
    else
    {
        CommandError(COMMAND_ERROR_GENERIC, "No info found for \"%s\"", argv[0]);
    }
}

static void CommandNext(int argc, char **argv)
{
    ServiceNowNextInfo_t *info = FindServiceName(argv[0]);
    if (info)
    {
        PrintEvent(&info->next);
    }    
    else
    {
        CommandError(COMMAND_ERROR_GENERIC, "No info found for \"%s\"", argv[0]);
    }    
}

static void PrintEvent(Event_t *event)
{
    CommandPrintf("Name: %s\n", event->name);
    CommandPrintf("Start time (Y/M/D H:M:S): %4d/%2d/%2d %02d:%02d:%02d\n",
        event->startTime.year, event->startTime.month, event->startTime.day,
        event->startTime.hour, event->startTime.minute, event->startTime.second);
    CommandPrintf("Duration (H:M:S): %02d:%02d:%02d\n", 
        event->duration.hour, event->duration.minute, event->duration.second);
    CommandPrintf("Description:\n%s\n", event->description);
}
/*******************************************************************************
* Filter Functions                                                             *
*******************************************************************************/
static void Init0x12Filter(PIDFilter_t *filter)
{
    serviceNowNextInfoList = ListCreate();
    filter->name = "Now/Next";
    filter->enabled = TRUE;
    SubTableProcessorInit(filter, 0x12, SubTableHandler, NULL, NULL, NULL);
}

static void Deinit0x12Filter(PIDFilter_t *filter)
{
    ListIterator_t iterator;
    filter->enabled = FALSE;
    SubTableProcessorDeinit(filter);
    ListFree(serviceNowNextInfoList, free);
}

static void SubTableHandler(void * arg, dvbpsi_handle demuxHandle, uint8_t tableId, uint16_t extension)
{
    switch (tableId)
    {
        case TABLE_ID_PF_ACTUAL: 
            dvbpsi_AttachEIT(demuxHandle, tableId, extension, ProcessEIT, NULL);
            break;
        case TABLE_ID_PF_OTHER: 
            dvbpsi_AttachEIT(demuxHandle, tableId, extension, ProcessEIT, NULL);
            break;
    }
}

static void ProcessEIT(void *arg, dvbpsi_eit_t *newEIT)
{
    dvbpsi_eit_event_t *event;
    ServiceNowNextInfo_t *info = FindService(newEIT->i_network_id, newEIT->i_ts_id, newEIT->i_service_id);
    printlog(LOG_DEBUG, "EIT received (version %d) net id %x ts id %x service id %x info %p\n",
        newEIT->i_version, newEIT->i_network_id, newEIT->i_ts_id, newEIT->i_service_id, info);
    if (!info)
    {
        info = calloc(1, sizeof(ServiceNowNextInfo_t));
        if (info)
        {
            ListAdd(serviceNowNextInfoList, info);
            info->networkId = newEIT->i_network_id;
            info->tsId = newEIT->i_ts_id;
            info->serviceId = newEIT->i_service_id;
        }
        else
        {
            return;
        }
    }

    memset(&info->now, 0, sizeof(Event_t));
    memset(&info->next, 0, sizeof(Event_t));
    if (newEIT->p_first_event)
    {
        UpdateEvent(&info->now, newEIT->p_first_event);
        if (newEIT->p_first_event->p_next)
        {
            UpdateEvent(&info->next, newEIT->p_first_event->p_next);
        }
    }

    dvbpsi_DeleteEIT(newEIT);
}

static void UpdateEvent(Event_t *event, dvbpsi_eit_event_t *eitevent)
{
    dvbpsi_descriptor_t *descriptor;
    dvbpsi_short_event_dr_t * sedescriptor;
    char startTime[5];
    startTime[0] = (eitevent->i_start_time >> 32) & 0xff;
    startTime[1] = (eitevent->i_start_time >> 24) & 0xff;    
    startTime[2] = (eitevent->i_start_time >> 16) & 0xff;        
    startTime[3] = (eitevent->i_start_time >>  8) & 0xff;            
    startTime[4] = (eitevent->i_start_time >>  0) & 0xff;                
    dvbpsi_DecodeMJDUTC(startTime, 
                        &event->startTime.year, &event->startTime.month, &event->startTime.day,
                        &event->startTime.hour,&event->startTime.minute, &event->startTime.second);
    event->duration.hour   = (((eitevent->i_duration >> 20 )& 0xf) * 10) + ((eitevent->i_duration >> 16 )& 0xf);
    event->duration.minute = (((eitevent->i_duration >> 12 )& 0xf) * 10) + ((eitevent->i_duration >>  8 )& 0xf);
    event->duration.second = (((eitevent->i_duration >>  4 )& 0xf) * 10) + ((eitevent->i_duration >>  0 )& 0xf);
                        
    for (descriptor = eitevent->p_first_descriptor; descriptor; descriptor = descriptor->p_next)
    {
        if (descriptor->i_tag == 0x4d)
        {
            sedescriptor = dvbpsi_DecodeShortEventDr(descriptor);
            memcpy(event->name, sedescriptor->i_event_name, sedescriptor->i_event_name_length);
            event->name[sedescriptor->i_event_name_length] = 0;
            memcpy(event->description, sedescriptor->i_text, sedescriptor->i_text_length);
            event->description[sedescriptor->i_text_length] = 0;
        }
    }
}

/*******************************************************************************
* Event List Helper Functions                                                  *
*******************************************************************************/
static ServiceNowNextInfo_t *FindServiceName(char *name)
{
    Service_t *service = ServiceFindName(name);
    Multiplex_t *multiplex;
    ServiceNowNextInfo_t *info;
    if (!service)
    {
        CommandError(COMMAND_ERROR_GENERIC, "Unknown service \"%s\"", name);
        return NULL;
    }

    multiplex = MultiplexFind(service->multiplexFreq);
    if (!multiplex)
    {
        CommandError(COMMAND_ERROR_GENERIC, "Failed to find multiplex!");
        ServiceRefDec(service);
        return NULL;
    }

    info = FindService(multiplex->networkId, multiplex->tsId, service->id);
    ServiceRefDec(service);
    MultiplexRefDec(multiplex);
    return info;
}

static ServiceNowNextInfo_t *FindService(uint16_t networkId, uint16_t tsId, uint16_t serviceId)
{
    ListIterator_t iterator;

    for (ListIterator_Init(iterator, serviceNowNextInfoList); 
         ListIterator_MoreEntries(iterator); 
         ListIterator_Next(iterator))
    {
        ServiceNowNextInfo_t *info = ListIterator_Current(iterator);

        if ((info->networkId == networkId) && (info->tsId == tsId) && (info->serviceId == serviceId))
        {
            return info;
        }
    }
    return NULL;
}
