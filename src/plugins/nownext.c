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
#include "main.h"
#include "dvbpsi/datetime.h"
#include "dvbpsi/eit.h"
#include "dvbpsi/dr_4d.h"

#include "list.h"
#include "logging.h"



/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/
#define MAX_STRING_LEN 256


/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/

typedef struct NNEvent_s
{
    char name[MAX_STRING_LEN];
    char description[MAX_STRING_LEN];
    struct tm startTime;
    uint32_t duration;
}NNEvent_t;

typedef struct ServiceNowNextInfo_s
{
    uint16_t networkId;
    uint16_t tsId;
    uint16_t serviceId;
    NNEvent_t  now;
    NNEvent_t  next;
}ServiceNowNextInfo_t;


/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void CommandNow(int argc, char **argv);
static void CommandNext(int argc, char **argv);
static void PrintEvent(NNEvent_t *event);
static void Install(bool installed);
static void NowNextFilterEventHandler(void *userArg, struct TSFilterGroup_t *group, TSFilterEventType_e event, void *details);
static ServiceNowNextInfo_t *FindServiceName(char *name);
static ServiceNowNextInfo_t *FindService(uint16_t networkId, uint16_t tsId, uint16_t serviceId);
static void SubTableHandler(void * arg, dvbpsi_handle demuxHandle, uint8_t tableId, uint16_t extension);
static void ProcessEIT(void *arg, dvbpsi_eit_t *newEIT);
static void UpdateEvent(NNEvent_t *event, dvbpsi_eit_event_t *eitevent);


/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static List_t *serviceNowNextInfoList;
static TSFilterGroup_t *tsgroup = NULL;
static dvbpsi_handle demux = NULL;
static char NOWNEXT[]="NowNext";

/*******************************************************************************
* Plugin Setup                                                                 *
*******************************************************************************/

PLUGIN_COMMANDS(
    {
        "now",
        1, 1,
        "Display the current program on the specified service.",
        "now <service>\n"
        "Display the current program on the specified service (assuming the data is "
        "present on the current TS).",
        CommandNow
    },
    {
        "next",
        1, 1,
        "Display the next program on the specified service.",
        "next <service>\n"
        "Display the next program on the specified service (assuming the data is "
        "present on the current TS).",
        CommandNext
    }
);

PLUGIN_FEATURES(
    PLUGIN_FEATURE_INSTALL(Install)
    );

PLUGIN_INTERFACE_CF(
    PLUGIN_FOR_DVB,
    "NowNext", "0.3",
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

static void PrintEvent(NNEvent_t *event)
{
    int h,m,s;
    time_t startTime = timegm(&event->startTime);
    time_t endTime = startTime + event->duration;
    CommandPrintf("Name       : %s\n", event->name);
    CommandPrintf("Start time : %s", ctime(&startTime));
    CommandPrintf("End time   : %s", ctime(&endTime));    
    h = event->duration / (60*60);
    m = (event->duration / 60) - (h * 60);
    s = event->duration - ((h * 60 * 60) + (m * 60));
    CommandPrintf("Duration   : %02d:%02d:%02d\n", h, m, s);
    CommandPrintf("Description:\n%s\n", event->description);
}
/*******************************************************************************
* Filter Functions                                                             *
*******************************************************************************/
static void Install(bool installed)
{
    if (installed)
    {
        serviceNowNextInfoList = ListCreate();
        tsgroup = TSReaderCreateFilterGroup(MainTSReaderGet(), "Now/Next", "DVB", NowNextFilterEventHandler,NULL);
    }
    else
    {
        TSFilterGroupDestroy(tsgroup);
        ListFree(serviceNowNextInfoList, free);
    }
}

static void NowNextFilterEventHandler(void *userArg, struct TSFilterGroup_t *group, TSFilterEventType_e event, void *details)
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
    ServiceNowNextInfo_t *info = FindService(newEIT->i_network_id, newEIT->i_ts_id, newEIT->i_service_id);
    LogModule(LOG_DEBUG, NOWNEXT, "EIT received (version %d) net id %x ts id %x service id %x info %p\n",
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

    ObjectRefDec(newEIT);
}

static void UpdateEvent(NNEvent_t *event, dvbpsi_eit_event_t *eitevent)
{
    dvbpsi_descriptor_t *descriptor;
    dvbpsi_short_event_dr_t * sedescriptor;

    event->startTime = eitevent->t_start_time;
    event->duration  = eitevent->i_duration;

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
    Service_t *service = ServiceFind(name);
    Multiplex_t *multiplex;
    ServiceNowNextInfo_t *info;
    if (!service)
    {
        CommandError(COMMAND_ERROR_GENERIC, "Unknown service \"%s\"", name);
        return NULL;
    }

    multiplex = MultiplexFindUID(service->multiplexUID);
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
