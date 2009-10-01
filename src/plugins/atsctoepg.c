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

atsctoepg.c

Plugin to collect EPG schedule information from ATSC/PSIP.

*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "plugin.h"
#include "epgchannel.h"
#include "dvbpsi/atsc/ett.h"
#include "dvbpsi/atsc/eit.h"


#include "main.h"
#include "list.h"
#include "logging.h"
#include "atsctext.h"
#include "tuning.h"
#include "deferredproc.h"


/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/
#define MAX_EITS 128 /* Maximum number of EIT tables (PIDs) */
#define MAX_ETTS 128 /* Maximum number of ETT tables (PIDs) */
/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/
typedef struct TableInfo_s
{
    uint16_t pid;
    dvbpsi_handle decoder;
}TableInfo_t;

typedef struct TSCEPGDeferredInfo_s
{
    uint16_t netId;
    uint16_t tsId;
    union {
    dvbpsi_atsc_ett_t *ett;
    dvbpsi_atsc_eit_t *eit;
    } u;
}ATSCEPGDeferredInfo_t;

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void Install(bool installed);

static void NewMGT(dvbpsi_atsc_mgt_t *newMGT);
static void NewSTT(dvbpsi_atsc_stt_t *newSTT);

static void ATSCtoEPGFilterGroupEventCallback(void *arg, TSFilterGroup_t *group, TSFilterEventType_e event, void *details);

static void ClearTableInfo(void);
static void StartEPGCapture(void);

static void SubTableHandler(void * arg, dvbpsi_handle demuxHandle, uint8_t tableId, uint16_t extension);
static void ProcessETT(void *arg, dvbpsi_atsc_ett_t *newETT);
static void ProcessEIT(void *arg, dvbpsi_atsc_eit_t *newEIT);
static void DeferredProcessEIT(void *arg);
static void DeferredProcessETT(void *arg);

static void ProcessEvent(EPGServiceRef_t *serviceRef, dvbpsi_atsc_eit_event_t *event);
static void DumpDescriptor(char *prefix, dvbpsi_descriptor_t *descriptor);
static void ConvertToTM(uint32_t startSeconds, uint32_t duration,
    struct tm *startTime, struct tm *endTime);

static void CommandEPGCapRestart(int argc, char **argv);
static void CommandEPGCapStart(int argc, char **argv);
static void CommandEPGCapStop(int argc, char **argv);
/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static const char ATSCTOEPG[] = "ATSCtoEPG";

static TSFilterGroup_t *tsgroup = NULL;
static uint8_t GPStoUTCSecondsOffset = 14; /* From the test streams 24th May 2007 */

static int EventInfoTableCount = 0;
static TableInfo_t EventInfoTableInfo[MAX_EITS];
static int ExtendedTextTableCount = 0;
static TableInfo_t ExtendedTextTableInfo[MAX_ETTS];

/*******************************************************************************
* Plugin Setup                                                                 *
*******************************************************************************/
PLUGIN_FEATURES(
    PLUGIN_FEATURE_INSTALL(Install),
    PLUGIN_FEATURE_MGTPROCESSOR(NewMGT),
    PLUGIN_FEATURE_STTPROCESSOR(NewSTT)
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
    PLUGIN_FOR_ATSC,
    "ATSCtoEPG", "0.3",
    "Plugin to capture ATSC EPG schedule information.",
    "charrea6@users.sourceforge.net"
    );
/*******************************************************************************
* Filter Functions                                                             *
*******************************************************************************/
static void Install(bool installed)
{
    if (installed)
    {
        ObjectRegisterType(ATSCEPGDeferredInfo_t);
    }
}


static void ClearTableInfo(void)
{
    int i;
    if (tsgroup)
    {
        TSFilterGroupRemoveAllFilters(tsgroup);
        for (i = 0; i < EventInfoTableCount; i ++)
        {
            dvbpsi_DetachDemux(EventInfoTableInfo[i].decoder);
        }
        for (i = 0; i < ExtendedTextTableCount; i ++)
        {
            dvbpsi_atsc_DetachETT(ExtendedTextTableInfo[i].decoder);
        }

    }
    EventInfoTableCount = 0;
    ExtendedTextTableCount = 0;
}

static void NewMGT(dvbpsi_atsc_mgt_t *newMGT)
{
    dvbpsi_atsc_mgt_table_t * table;
    ClearTableInfo();

    for (table = newMGT->p_first_table; table; table = table->p_next)
    {
        if ((table->i_type >= 0x100) && (table->i_type <= 0x17f))
        {
            EventInfoTableInfo[EventInfoTableCount].pid = table->i_pid;
            EventInfoTableCount ++;
        }
        if ((table->i_type >= 0x200) && (table->i_type <= 0x27f))
        {
            ExtendedTextTableInfo[ExtendedTextTableCount].pid = table->i_pid;
            ExtendedTextTableCount ++;
        }
    }
    if (tsgroup)
    {
        StartEPGCapture();
    }
}

static void NewSTT(dvbpsi_atsc_stt_t *newSTT)
{
    GPStoUTCSecondsOffset = newSTT->i_gps_utc_offset;
}

static void ATSCtoEPGFilterGroupEventCallback(void *arg, TSFilterGroup_t *group, TSFilterEventType_e event, void *details)
{
    if (event == TSFilterEventType_MuxChanged)
    {
        ClearTableInfo();
    }
}


static void SubTableHandler(void * arg, dvbpsi_handle demuxHandle, uint8_t tableId, uint16_t extension)
{
    dvbpsi_atsc_AttachEIT(demuxHandle, tableId, extension, ProcessEIT, NULL);
}

static void ProcessETT(void *arg, dvbpsi_atsc_ett_t *newETT)
{
    Multiplex_t *multiplex = TuningCurrentMultiplexGet();
    ATSCEPGDeferredInfo_t *info = ObjectCreateType(ATSCEPGDeferredInfo_t);
    info->netId = multiplex->networkId;
    info->tsId = multiplex->tsId;
    info->u.ett = newETT;
    DeferredProcessingAddJob(DeferredProcessETT, info);
    ObjectRefDec(info);
    MultiplexRefDec(multiplex);    
}

static void ProcessEIT(void *arg, dvbpsi_atsc_eit_t *newEIT)
{
    Multiplex_t *multiplex = TuningCurrentMultiplexGet();
    ATSCEPGDeferredInfo_t *info = ObjectCreateType(ATSCEPGDeferredInfo_t);
    info->netId = multiplex->networkId;
    info->tsId = multiplex->tsId;
    info->u.eit = newEIT;
    DeferredProcessingAddJob(DeferredProcessEIT, info);
    ObjectRefDec(info);
    MultiplexRefDec(multiplex);    
}

static void DeferredProcessEIT(void *arg)
{
    ATSCEPGDeferredInfo_t *info = (ATSCEPGDeferredInfo_t*)arg;
    dvbpsi_atsc_eit_t *eit = info->u.eit;
    
    EPGServiceRef_t serviceRef;
    dvbpsi_atsc_eit_event_t *event;

    LogModule(LOG_DEBUG, ATSCTOEPG, "Processing EIT (version %d) source id %x\n",
    eit->i_version, eit->i_source_id);

    serviceRef.netId = info->netId;
    serviceRef.tsId = info->tsId;
    serviceRef.serviceId = eit->i_source_id;
    for (event = eit->p_first_event; event; event = event->p_next)
    {
        ProcessEvent(&serviceRef, event);
    }
    ObjectRefDec(eit);
    ObjectRefDec(info);
}

static void DeferredProcessETT(void *arg)
{
    ATSCEPGDeferredInfo_t *info = (ATSCEPGDeferredInfo_t*)arg;
    ATSCMultipleStrings_t *description;
    dvbpsi_atsc_ett_t *ett = info->u.ett;
    EPGEventRef_t eventRef;
    char lang[4];
    int i;

    eventRef.serviceRef.netId = info->netId;
    eventRef.serviceRef.tsId = info->tsId;
    eventRef.serviceRef.serviceId = (ett->i_etm_id >> 16) & 0xffff;
    eventRef.eventId = (ett->i_etm_id & 0xffff) >> 2;
    lang[3] = 0;

    description = ATSCMultipleStringsConvert(ett->p_etm, ett->i_etm_length);
    LogModule(LOG_DEBUG, ATSCTOEPG,"Processing ETT for %04x.%04x.%04x.%04x (%08x): Number of strings %d\n",
        eventRef.serviceRef.netId, eventRef.serviceRef.tsId, eventRef.serviceRef.serviceId, eventRef.eventId,
        ett->i_etm_id, description->number_of_strings);
    
    for (i = 0; i < description->number_of_strings; i ++)
    {

        LogModule(LOG_DEBUG, ATSCTOEPG, "%d : (%c%c%c) %s\n",
            i + 1, description->strings[i].lang[0],description->strings[i].lang[1],description->strings[i].lang[2],
            description->strings[i].text);
        lang[0] = description->strings[i].lang[0];
        lang[1] = description->strings[i].lang[1];
        lang[2] = description->strings[i].lang[2];
        EPGChannelNewDetail(&eventRef, lang, EPG_EVENT_DETAIL_DESCRIPTION,description->strings[i].text);
    }

    ObjectRefDec(description);

    ObjectRefDec(ett);
    ObjectRefDec(info);
}

/*******************************************************************************
* Command Functions                                                            *
*******************************************************************************/
static void CommandEPGCapRestart(int argc, char **argv)
{
    if (!tsgroup)
    {
        CommandEPGCapStart(argc, argv);
        return;
    }
    ClearTableInfo();
    StartEPGCapture();
}

static void CommandEPGCapStart(int argc, char **argv)
{
    tsgroup = TSReaderCreateFilterGroup(MainTSReaderGet(), ATSCTOEPG, "ATSC", ATSCtoEPGFilterGroupEventCallback, NULL);
    StartEPGCapture();
}

static void CommandEPGCapStop(int argc, char **argv)
{
    ClearTableInfo();
    TSFilterGroupDestroy(tsgroup);
    tsgroup = NULL;
}

/*******************************************************************************
* Helper Functions                                                             *
*******************************************************************************/
static void StartEPGCapture(void)
{
    int i;
    for (i = 0; i < EventInfoTableCount; i ++)
    {
        EventInfoTableInfo[i].decoder = dvbpsi_AttachDemux(SubTableHandler, NULL);
        TSFilterGroupAddSectionFilter(tsgroup, EventInfoTableInfo[i].pid, 3, EventInfoTableInfo[i].decoder);
    }
    for (i = 0; i < ExtendedTextTableCount; i ++)
    {
        ExtendedTextTableInfo[i].decoder = dvbpsi_atsc_AttachETT(ProcessETT, NULL);
        TSFilterGroupAddSectionFilter(tsgroup,ExtendedTextTableInfo[i].pid, 3, ExtendedTextTableInfo[i].decoder);
    }
}

static void ProcessEvent(EPGServiceRef_t *serviceRef, dvbpsi_atsc_eit_event_t *eitevent)
{
    EPGEventRef_t eventRef;
    dvbpsi_descriptor_t *descriptor;
    struct tm startTime;
    struct tm endTime;
    char startTimeStr[25];
    char endTimeStr[25];
    ATSCMultipleStrings_t *title;
    int i;
    char lang[4];

    eventRef.serviceRef = *serviceRef;
    eventRef.eventId = eitevent->i_event_id;
    
    ConvertToTM(eitevent->i_start_time, eitevent->i_length_seconds, &startTime, &endTime);

    strftime(startTimeStr, sizeof(startTimeStr), "%Y-%m-%d %T", &startTime);
    strftime(endTimeStr, sizeof(startTimeStr), "%Y-%m-%d %T", &endTime);
    LogModule(LOG_DEBUG, ATSCTOEPG, "Processing EIT for %04x.%04x.%04x.%04x Start Time %s (%d) End Time %s (duration %d) Title Length %d ETM location=%d\n",
        eventRef.serviceRef.netId, eventRef.serviceRef.tsId, eventRef.serviceRef.serviceId, eventRef.eventId,
        startTimeStr,eitevent->i_start_time, endTimeStr, eitevent->i_length_seconds, eitevent->i_title_length, eitevent->i_etm_location);

    if (EPGChannelNewEvent(&eventRef, &startTime, &endTime, FALSE) != 0)
    {
        return;
    }

    lang[3] = 0;
    title = ATSCMultipleStringsConvert(eitevent->i_title, eitevent->i_title_length);
    for (i = 0; i < title->number_of_strings; i ++)
    {

        LogModule(LOG_DEBUG, ATSCTOEPG, "%d : (%c%c%c) %s\n",
            i + 1, title->strings[i].lang[0],title->strings[i].lang[1],title->strings[i].lang[2],
            title->strings[i].text);
        lang[0] = title->strings[i].lang[0];
        lang[1] = title->strings[i].lang[1];
        lang[2] = title->strings[i].lang[2];
        EPGChannelNewDetail(&eventRef, lang, EPG_EVENT_DETAIL_TITLE,title->strings[i].text);
    }
    ObjectRefDec(title);

    LogModule(LOG_DEBUGV, ATSCTOEPG, "Start of Descriptors\n");
    for (descriptor = eitevent->p_first_descriptor; descriptor; descriptor = descriptor->p_next)
    {
        DumpDescriptor("\t", descriptor);
    }
    LogModule(LOG_DEBUGV, ATSCTOEPG, "End of Descriptors:\n");
}

static void ConvertToTM(uint32_t startSeconds, uint32_t duration,
    struct tm *startTime, struct tm *endTime)
{
    struct tm *temp_time;
    time_t secs;

    secs = startSeconds + dvbpsi_atsc_unix_epoch_offset - GPStoUTCSecondsOffset;
    temp_time = gmtime(&secs);
    *startTime = *temp_time;

    secs += duration;

    temp_time = gmtime(&secs);
    *endTime = *temp_time;
}

static void DumpDescriptor(char *prefix, dvbpsi_descriptor_t *descriptor)
{
    int i;
    char line[(16 * 3) + 1];
    line[0] = 0;
    LogModule(LOG_DEBUGV, ATSCTOEPG, "%sTag : 0x%02x (Length %d)\n", prefix, descriptor->i_tag, descriptor->i_length);
    for (i = 0; i < descriptor->i_length; i ++)
    {
        if (i && ((i % 16) == 0))
        {
            LogModule(LOG_DEBUGV, ATSCTOEPG, "%s%s\n", prefix, line);
            line[0] = 0;
        }
        sprintf(line + strlen(line), "%02x ", descriptor->p_data[i]);
    }
    if (line[0])
    {
        LogModule(LOG_DEBUGV, ATSCTOEPG, "%s%s\n", prefix, line);
    }
}

