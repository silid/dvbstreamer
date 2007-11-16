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
#include "epgdbase.h"
#include "dvbpsi/atsc/eit.h"


#include "list.h"
#include "logging.h"
#include "subtableprocessor.h"
#include "atsctext.h"
#include "tuning.h"
#include "deferredproc.h"


/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/
#define MAX_EITS 128 /* Maximum number of EIT tables (PIDs) */

/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/
typedef struct EITInfo_s
{
    uint16_t pid;
    dvbpsi_handle demux;
}EITInfo_t;

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void InitEITFilter(PIDFilter_t *filter);
static void DeinitEITFilter(PIDFilter_t *filter);

static void NewMGT(dvbpsi_atsc_mgt_t *newMGT);

static int ATSCtoEPGFilterPacket(PIDFilter_t *pidfilter, void *arg, uint16_t pid, TSPacket_t *packet);
static TSPacket_t * ATSCtoEPGProcessPacket(PIDFilter_t *pidfilter, void *arg, TSPacket_t *packet);
static void ATSCtoEPGMultiplexChanged(PIDFilter_t *pidfilter, void *arg, Multiplex_t *newmultiplex);

static void ClearEITInfo(void);

static void SubTableHandler(void * arg, dvbpsi_handle demuxHandle, uint8_t tableId, uint16_t extension);
static void ProcessEIT(void *arg, dvbpsi_atsc_eit_t *newEIT);
static void DeferredProcessEIT(void *arg);

static void ProcessEvent(EPGServiceRef_t *serviceRef, dvbpsi_atsc_eit_event_t *event);
static void DumpDescriptor(char *prefix, dvbpsi_descriptor_t *descriptor);
static void ConvertToTM(uint32_t startSeconds, uint32_t duration,
    struct tm *startTime, struct tm *endTime);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static PluginFilter_t filter = {NULL, InitEITFilter, DeinitEITFilter};
static const char ATSCTOEPG[] = "ATSCtoEPG";

static int EITCount = 0;
static EITInfo_t EITInfo[MAX_EITS];
static time_t UnixEpochOffset;

/*******************************************************************************
* Plugin Setup                                                                 *
*******************************************************************************/
#ifdef __CYGWIN__
#define PluginInterface ATSCtoEPGPluginInterface
#endif

PLUGIN_FEATURES(
    PLUGIN_FEATURE_FILTER(filter),
    PLUGIN_FEATURE_MGTPROCESSOR(NewMGT)
    );

PLUGIN_INTERFACE_F(
    PLUGIN_FOR_ATSC,
    "ATSCtoEPG", "0.1", 
    "Plugin to capture ATSC EPG schedule information.", 
    "charrea6@users.sourceforge.net"
    );
/*******************************************************************************
* Filter Functions                                                             *
*******************************************************************************/
static void InitEITFilter(PIDFilter_t *filter)
{
    struct tm temp;

    filter->name = "ATSC to EPG";
    filter->enabled = TRUE;
    PIDFilterFilterPacketSet(filter, ATSCtoEPGFilterPacket, NULL);
    PIDFilterMultiplexChangeSet(filter, ATSCtoEPGMultiplexChanged, NULL);
    PIDFilterProcessPacketSet(filter, ATSCtoEPGProcessPacket, NULL);

    memset(&temp, 0, sizeof(temp));
    temp.tm_year = 80;
    temp.tm_mon = 0;
    temp.tm_mday = 6;

    UnixEpochOffset = mktime(&temp);
    
}

static void DeinitEITFilter(PIDFilter_t *filter)
{
    filter->enabled = FALSE;

    ClearEITInfo();
}


static void ClearEITInfo(void)
{
    int i;
    for (i = 0; i < EITCount; i ++)
    {
        dvbpsi_DetachDemux(EITInfo[i].demux);
    }
    EITCount = 0;
}

static void NewMGT(dvbpsi_atsc_mgt_t *newMGT)
{
    dvbpsi_atsc_mgt_table_t * table;
    ClearEITInfo();

    for (table = newMGT->p_first_table; table; table = table->p_next)
    {
        if ((table->i_type >= 0x100) && (table->i_type <= 0x17f))
        {
            EITInfo[EITCount].pid = table->i_pid;
            EITInfo[EITCount].demux = dvbpsi_AttachDemux(SubTableHandler, NULL);
            EITCount ++;
        }
    }
}

static int ATSCtoEPGFilterPacket(PIDFilter_t *pidfilter, void *arg, uint16_t pid, TSPacket_t *packet)
{
    int i;
    for (i = 0; i < EITCount; i ++)
    {
        if (EITInfo[i].pid == pid)
        {
            return TRUE;
        }
    }
    return FALSE;
}

static void ATSCtoEPGMultiplexChanged(PIDFilter_t *pidfilter, void *arg, Multiplex_t *newmultiplex)
{
    ClearEITInfo();
}

static TSPacket_t * ATSCtoEPGProcessPacket(PIDFilter_t *pidfilter, void *arg, TSPacket_t *packet)
{
    int i;
    uint16_t pid = TSPACKET_GETPID(*packet);
    for (i = 0; i < EITCount; i ++)
    {
        if (EITInfo[i].pid == pid)
        {
            dvbpsi_PushPacket(EITInfo[i].demux, (uint8_t*)packet);
        }
    }
    return NULL;
}

static void SubTableHandler(void * arg, dvbpsi_handle demuxHandle, uint8_t tableId, uint16_t extension)
{
    dvbpsi_atsc_AttachEIT(demuxHandle, tableId, extension, ProcessEIT, NULL);
}

static void ProcessEIT(void *arg, dvbpsi_atsc_eit_t *newEIT)
{
    DeferredProcessingAddJob(DeferredProcessEIT, newEIT);
}

static void DeferredProcessEIT(void *arg)
{
    dvbpsi_atsc_eit_t *eit = (dvbpsi_atsc_eit_t *)arg;
    Multiplex_t *multiplex = TuningCurrentMultiplexGet();
    EPGServiceRef_t serviceRef;
    dvbpsi_atsc_eit_event_t *event;

    LogModule(LOG_DEBUG, ATSCTOEPG, "Processing EIT (version %d) source id %x\n",
    eit->i_version, eit->i_source_id);
    
    EPGDBaseTransactionStart();

    serviceRef.netId = multiplex->networkId;
    serviceRef.tsId = multiplex->tsId;
    serviceRef.serviceId = eit->i_source_id;
    for (event = eit->p_first_event; event; event = event->p_next)
    {
        ProcessEvent(&serviceRef, event);
    }
    ObjectRefDec(eit);
    EPGDBaseTransactionCommit();

    MultiplexRefDec(multiplex);
}
/*******************************************************************************
* Helper Functions                                                             *
*******************************************************************************/
static void ProcessEvent(EPGServiceRef_t *serviceRef, dvbpsi_atsc_eit_event_t *eitevent)
{
    EPGEvent_t epgevent;
    dvbpsi_descriptor_t *descriptor;
    char startTimeStr[25];
    char endTimeStr[25];
    ATSCMultipleStrings_t *title;
    int i;
    char lang[4];
    
    epgevent.serviceRef = *serviceRef;
    epgevent.eventId = eitevent->i_event_id;
    ConvertToTM(eitevent->i_start_time, eitevent->i_length_seconds, &epgevent.startTime, &epgevent.endTime);
    epgevent.ca = FALSE;
    strftime(startTimeStr, sizeof(startTimeStr), "%Y-%m-%d %T", &epgevent.startTime);
    strftime(endTimeStr, sizeof(startTimeStr), "%Y-%m-%d %T", &epgevent.endTime);
    LogModule(LOG_DEBUG, ATSCTOEPG, "(%x:%x:%x) Event %x Start Time %s (%d) End Time %s (duration %d) Title Length %d\n",
        serviceRef->netId, serviceRef->tsId, serviceRef->serviceId, epgevent.eventId,
        startTimeStr,eitevent->i_start_time, endTimeStr,eitevent->i_length_seconds, eitevent->i_title_length);
    
    if (EPGDBaseEventAdd(&epgevent) != 0)
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
        EPGDBaseDetailAdd(serviceRef,epgevent.eventId, lang, EPG_EVENT_DETAIL_TITLE,title->strings[i].text);
    }
    ObjectRefDec(title);
    
    LogModule(LOG_DEBUG, ATSCTOEPG, "Start of Descriptors\n");
    for (descriptor = eitevent->p_first_descriptor; descriptor; descriptor = descriptor->p_next)
    {
        DumpDescriptor("\t", descriptor);
    }
    LogModule(LOG_DEBUG, ATSCTOEPG, "End of Descriptors:\n");    
}

static void ConvertToTM(uint32_t startSeconds, uint32_t duration,
    struct tm *startTime, struct tm *endTime)
{
    struct tm *temp_time;
    time_t secs;

    secs = startSeconds + UnixEpochOffset;
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
    LogModule(LOG_DEBUG, ATSCTOEPG, "%sTag : 0x%02x (Length %d)\n", prefix, descriptor->i_tag, descriptor->i_length);
    for (i = 0; i < descriptor->i_length; i ++)
    {
        if (i && ((i % 16) == 0))
        {
            LogModule(LOG_DEBUG, ATSCTOEPG, "%s%s\n", prefix, line);
            line[0] = 0;
        }
        sprintf(line + strlen(line), "%02x ", descriptor->p_data[i]);
    }
    if (line[0])
    {
        LogModule(LOG_DEBUG, ATSCTOEPG, "%s%s\n", prefix, line);
    }
}

