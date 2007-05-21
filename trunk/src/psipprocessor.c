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

psipprocessor.c

Process ATSC PSIP tables

*/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <assert.h>
#include <iconv.h>

#include <dvbpsi/dvbpsi.h>
#include <dvbpsi/descriptor.h>
#include <dvbpsi/dr.h>
#include <dvbpsi/demux.h>
#include "dvbpsi/mgt.h"
#include "dvbpsi/stt.h"
#include "dvbpsi/vct.h"

#include "multiplexes.h"
#include "services.h"
#include "pids.h"
#include "dvb.h"
#include "ts.h"
#include "main.h"
#include "cache.h"
#include "logging.h"
#include "list.h"
#include "tuning.h"
#include "subtableprocessor.h"
#include "psipprocessor.h"

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void SubTableHandler(void * state, dvbpsi_handle demuxHandle, uint8_t tableId, uint16_t extension);
static void ProcessMGT(void *arg, dvbpsi_mgt_t *newMGT);
static void ProcessSTT(void *arg, dvbpsi_stt_t *newSTT);
static void ProcessVCT(void *arg, dvbpsi_vct_t *newVCT);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static const char PSIPPROCESSOR[] = "PSIPProcessor";

static List_t *NewMGTCallbacksList = NULL;
static List_t *NewSTTCallbacksList = NULL;
static List_t *NewVCTCallbacksList = NULL;
static iconv_t utf16ToUtf8CD;

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
int PSIPProcessorInit(void)
{
    utf16ToUtf8CD = iconv_open("UTF-8", "UTF-16BE");
    if ((long)utf16ToUtf8CD == -1)
    {
        LogModule(LOG_ERROR, PSIPPROCESSOR, "Failed to open iconv to convert UTF16 to UTF8\n");
        return -1;
    }

    NewMGTCallbacksList = ListCreate();
    NewSTTCallbacksList = ListCreate();
    NewVCTCallbacksList = ListCreate();


    return (NewMGTCallbacksList && NewSTTCallbacksList && NewVCTCallbacksList) ? 0 : -1;
}

void PSIPProcessorDeInit(void)
{
    ListFree(NewMGTCallbacksList, NULL);
    ListFree(NewSTTCallbacksList, NULL);
    ListFree(NewVCTCallbacksList, NULL);    
    iconv_close(utf16ToUtf8CD);
}

PIDFilter_t *PSIPProcessorCreate(TSFilter_t *tsfilter)
{
    PIDFilter_t *result = SubTableProcessorCreate(tsfilter, 0x1ffb, SubTableHandler, NULL, NULL, NULL);
    if (result)
    {
        result->name = "PSIP";
    }
    
    return result;
}

void PSIPProcessorDestroy(PIDFilter_t *filter)
{
    SubTableProcessorDestroy(filter);
}

void PSIPProcessorRegisterMGTCallback(PluginMGTProcessor_t callback)
{
    if (NewMGTCallbacksList)
    {
        ListAdd(NewMGTCallbacksList, callback);
    }
}

void PSIPProcessorUnRegisterMGTCallback(PluginMGTProcessor_t callback)
{
    if (NewMGTCallbacksList)
    {
        ListRemove(NewMGTCallbacksList, callback);
    }
}

void PSIPProcessorRegisterSTTCallback(PluginSTTProcessor_t callback)
{
    if (NewSTTCallbacksList)
    {
        ListAdd(NewSTTCallbacksList, callback);
    }
}

void PSIPProcessorUnRegisterSTTCallback(PluginSTTProcessor_t callback)
{
    if (NewSTTCallbacksList)
    {
        ListRemove(NewSTTCallbacksList, callback);
    }
}

void PSIPProcessorRegisterVCTCallback(PluginVCTProcessor_t callback)
{
    if (NewVCTCallbacksList)
    {
        LogModule(LOG_DEBUG, PSIPPROCESSOR, "Registered %p\n", callback);
        ListAdd(NewVCTCallbacksList, callback);
    }
}

void PSIPProcessorUnRegisterVCTCallback(PluginVCTProcessor_t callback)
{
    if (NewVCTCallbacksList)
    {
        ListRemove(NewVCTCallbacksList, callback);
    }
}

/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/

static void SubTableHandler(void * arg, dvbpsi_handle demuxHandle, uint8_t tableId, uint16_t extension)
{
    switch (tableId)
    {
        /* MGT */
        case 0xC7:
            dvbpsi_AttachMGT(demuxHandle, tableId, ProcessMGT, arg);
            break;
        /* TVCT */
        case 0xC8: /* Fall through intended */
        /* CVCT */
        case 0xC9:
            {
                Multiplex_t *current = TuningCurrentMultiplexGet();
                /* Currently only handle VCT for the current multiplex */
                if (extension == current->tsId)
                {
                    dvbpsi_AttachVCT(demuxHandle, tableId, extension, ProcessVCT, arg);
                }
                MultiplexRefDec(current);
            }
            break;
        /* RRT */
        case 0xCA:
            break;
        /* STT */
        case 0xCD:
            dvbpsi_AttachSTT(demuxHandle,tableId, ProcessSTT, arg);
            break;
    }
}

static void ProcessMGT(void *arg, dvbpsi_mgt_t *newMGT)
{
    dvbpsi_mgt_table_t *table;
    ListIterator_t iterator;
    Multiplex_t *current = TuningCurrentMultiplexGet();
    if (current->networkId == -1)
    {
        CacheUpdateNetworkId(current, current->freq / 1000000);
    }
    MultiplexRefDec(current);
    LogModule(LOG_DEBUG, PSIPPROCESSOR,"New MGT Received! Version %d Protocol %d\n", newMGT->i_version, newMGT->i_protocol);
    for (table = newMGT->p_first_table; table; table = table->p_next)
    {
        LogModule(LOG_DEBUG, PSIPPROCESSOR, "\tType=%d PID=%d Version=%d number bytes=%d\n",
            table->i_type, table->i_pid, table->i_version, table->i_number_bytes);
    }


    for (ListIterator_Init(iterator, NewMGTCallbacksList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        PluginMGTProcessor_t callback = ListIterator_Current(iterator);
        callback(newMGT);
    }
    
    dvbpsi_DeleteMGT(newMGT);
}

static void ProcessSTT(void *arg, dvbpsi_stt_t *newSTT)
{
    ListIterator_t iterator;
    struct tm gpsEpoch;
    time_t gpsEpochSeconds;
    time_t utcSeconds;
    LogModule(LOG_DEBUG, PSIPPROCESSOR,"New STT Received! Protocol %d GPS Time =%lu GPS->UTC Offset = %u \n",
            newSTT->i_protocol, newSTT->i_system_time, newSTT->i_gps_utc_offset);
    gpsEpoch.tm_hour = 0;
    gpsEpoch.tm_min  = 0;
    gpsEpoch.tm_sec  = 0;
    gpsEpoch.tm_mday = 6;
    gpsEpoch.tm_mon  = 0;
    gpsEpoch.tm_year = 80;
    gpsEpochSeconds = mktime(&gpsEpoch);
    utcSeconds = gpsEpochSeconds + newSTT->i_system_time -  newSTT->i_gps_utc_offset;

    LogModule(LOG_DEBUG, PSIPPROCESSOR, "STT UTC Time = %s\n", asctime(gmtime(&utcSeconds)));

    for (ListIterator_Init(iterator, NewSTTCallbacksList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        PluginSTTProcessor_t callback = ListIterator_Current(iterator);
        callback(newSTT);
    }
    
    dvbpsi_DeleteSTT(newSTT);
}

static void ProcessVCT(void *arg, dvbpsi_vct_t *newVCT)
{
    ListIterator_t iterator;

    dvbpsi_vct_channel_t *channel;
    LogModule(LOG_DEBUG, PSIPPROCESSOR, "New VCT Recieved! Version %d Protocol %d Cable VCT? %s\n", 
        newVCT->i_version, newVCT->i_protocol, newVCT->b_cable_vct ? "Yes":"No");

    for (channel = newVCT->p_first_channel; channel; channel = channel->p_next)
    {
        char serviceName[(7 * 6) + 1];
        char *inbuf;
        size_t inbytes;
        char *outbuf;
        size_t outbytes;
        int ret;
        inbuf = (char *)channel->i_short_name;
        inbytes = 8;
        outbuf = serviceName;
        outbytes = sizeof(serviceName);
        ret = iconv(utf16ToUtf8CD, (char **) &inbuf, &inbytes, &outbuf, &outbytes);
        if (ret == -1)
        {
            LogModule(LOG_ERROR, PSIPPROCESSOR, "Failed to convert service name\n");
        }
        else
        {
            Service_t *service = CacheServiceFindId(channel->i_program_number);
            *outbuf = 0;
            if (service)
            {
                if (service->source != channel->i_source_id)
                {
                    CacheUpdateServiceSource(service, channel->i_source_id);
                }
                if (strcmp(service->name, serviceName))
                {
                    CacheUpdateServiceName(service, serviceName);
                }

                LogModule(LOG_DEBUG, PSIPPROCESSOR, "\t%d-%d %s\n", channel->i_major_number, channel->i_minor_number, serviceName);
                LogModule(LOG_DEBUG, PSIPPROCESSOR, "\t\tTS ID          = %04x\n", channel->i_channel_tsid);
                LogModule(LOG_DEBUG, PSIPPROCESSOR, "\t\tProgram number = %04x\n", channel->i_program_number);
                LogModule(LOG_DEBUG, PSIPPROCESSOR, "\t\tSource id      = %04x\n", channel->i_source_id);
                LogModule(LOG_DEBUG, PSIPPROCESSOR, "\t\tService type   = %d\n", channel->i_service_type);
                ServiceRefDec(service);
            }
        }
        
    }
    
    for (ListIterator_Init(iterator, NewVCTCallbacksList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        PluginVCTProcessor_t callback = ListIterator_Current(iterator);
        callback(newVCT);
    }

    dvbpsi_DeleteVCT(newVCT);
        
}

