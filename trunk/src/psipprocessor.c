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
#include "dvbpsi/atsc/mgt.h"
#include "dvbpsi/atsc/stt.h"
#include "dvbpsi/atsc/vct.h"

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
#include "atsctext.h"

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void SubTableHandler(void * state, dvbpsi_handle demuxHandle, uint8_t tableId, uint16_t extension);
static void ProcessMGT(void *arg, dvbpsi_atsc_mgt_t *newMGT);
static void ProcessSTT(void *arg, dvbpsi_atsc_stt_t *newSTT);
static void ProcessVCT(void *arg, dvbpsi_atsc_vct_t *newVCT);

static void DumpDescriptor(char *prefix, dvbpsi_descriptor_t *descriptor);

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
            dvbpsi_atsc_AttachMGT(demuxHandle, tableId, ProcessMGT, arg);
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
                    dvbpsi_atsc_AttachVCT(demuxHandle, tableId, extension, ProcessVCT, arg);
                }
                MultiplexRefDec(current);
            }
            break;
        /* RRT */
        case 0xCA:
            break;
        /* STT */
        case 0xCD:
            dvbpsi_atsc_AttachSTT(demuxHandle,tableId, ProcessSTT, arg);
            break;
    }
}

static void ProcessMGT(void *arg, dvbpsi_atsc_mgt_t *newMGT)
{
    dvbpsi_atsc_mgt_table_t *table;
    ListIterator_t iterator;
    dvbpsi_descriptor_t *descriptor;    
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
        LogModule(LOG_DEBUG, PSIPPROCESSOR, "\tStart of Descriptors\n");
        for (descriptor = table->p_first_descriptor; descriptor; descriptor = descriptor->p_next)
        {
            DumpDescriptor("\t\t\t",descriptor);
        }
        LogModule(LOG_DEBUG, PSIPPROCESSOR, "\tEnd of Descriptors\n");        
    }
    LogModule(LOG_DEBUG, PSIPPROCESSOR, "\tStart of Descriptors\n");
    for (descriptor = newMGT->p_first_descriptor; descriptor; descriptor = descriptor->p_next)
    {
        DumpDescriptor("\t\t",descriptor);
    }
    LogModule(LOG_DEBUG, PSIPPROCESSOR, "\tEnd of Descriptors\n");        


    for (ListIterator_Init(iterator, NewMGTCallbacksList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        PluginMGTProcessor_t callback = ListIterator_Current(iterator);
        callback(newMGT);
    }
    
    dvbpsi_atsc_DeleteMGT(newMGT);
}

static void ProcessSTT(void *arg, dvbpsi_atsc_stt_t *newSTT)
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
    
    dvbpsi_atsc_DeleteSTT(newSTT);
}

static void ProcessVCT(void *arg, dvbpsi_atsc_vct_t *newVCT)
{
    ListIterator_t iterator;
    dvbpsi_descriptor_t *descriptor; 
    dvbpsi_atsc_vct_channel_t *channel;
    LogModule(LOG_DEBUG, PSIPPROCESSOR, "New VCT Recieved! Version %d Protocol %d Cable VCT? %s TS Id = 0x%04x\n", 
        newVCT->i_version, newVCT->i_protocol, newVCT->b_cable_vct ? "Yes":"No", newVCT->i_ts_id);
    
    for (channel = newVCT->p_first_channel; channel; channel = channel->p_next)
    {
        char serviceName[10 + (7 * 6) + 1];
        char *inbuf;
        size_t inbytes;
        char *outbuf;
        size_t outbytes;
        int ret;
        /* Prepend the channels major-minor number to the name to get round 
         * problems with broadcasters not using a unique name for each channel! 
         */
        sprintf(serviceName, "%d-%d ", channel->i_major_number, channel->i_minor_number);
        
        inbuf = (char *)channel->i_short_name;
        inbytes = 14;
        outbuf = serviceName + strlen(serviceName);
        outbytes = sizeof(serviceName) - 10;

        ret = iconv(utf16ToUtf8CD, (ICONV_INPUT_CAST) &inbuf, &inbytes, &outbuf, &outbytes);
        if (ret == -1)
        {
            LogModule(LOG_ERROR, PSIPPROCESSOR, "Failed to convert service name\n");
        }
        else
        {
            Service_t *service = CacheServiceFindId(channel->i_program_number);
            *outbuf = 0;

            if (!service)
            {
               service = CacheServiceAdd(channel->i_program_number);
            }

            if (service->source != channel->i_source_id)
            {
                CacheUpdateServiceSource(service, channel->i_source_id);
            }
            if (strcmp(service->name, serviceName))
            {
                CacheUpdateServiceName(service, serviceName);
            }

            LogModule(LOG_DEBUG, PSIPPROCESSOR, "\t%s\n", serviceName);
            LogModule(LOG_DEBUG, PSIPPROCESSOR, "\t\tTS ID          = %04x\n", channel->i_channel_tsid);
            LogModule(LOG_DEBUG, PSIPPROCESSOR, "\t\tProgram number = %04x\n", channel->i_program_number);
            LogModule(LOG_DEBUG, PSIPPROCESSOR, "\t\tSource id      = %04x\n", channel->i_source_id);
            LogModule(LOG_DEBUG, PSIPPROCESSOR, "\t\tService type   = %d\n", channel->i_service_type);
            ServiceRefDec(service);

        }
        LogModule(LOG_DEBUG, PSIPPROCESSOR, "\tStart of Descriptors\n");
        for (descriptor = channel->p_first_descriptor; descriptor; descriptor = descriptor->p_next)
        {
            DumpDescriptor("\t\t\t",descriptor);
            if (descriptor->i_tag == 0xa0)
            {
                ATSCMultipleStrings_t *strings = ATSCMultipleStringsConvert(descriptor->p_data, descriptor->i_length);
                int s;
                for (s = 0; s < strings->number_of_strings; s ++)
                {
                    LogModule(LOG_DEBUG, PSIPPROCESSOR, "\t\t\t\t%d (%c%c%c): %s\n", s,  
                        strings->strings[s].lang[0],strings->strings[s].lang[1],strings->strings[s].lang[2],
                        strings->strings[s].text);
                }
                ObjectRefDec(strings);
                
            }
        }
        LogModule(LOG_DEBUG, PSIPPROCESSOR, "\tEnd of Descriptors\n");
        
    }
    for (descriptor = newVCT->p_first_descriptor; descriptor; descriptor = descriptor->p_next)
    {
        DumpDescriptor("\t\t",descriptor);
    }
    LogModule(LOG_DEBUG, PSIPPROCESSOR, "\tEnd of Descriptors\n");
    
    for (ListIterator_Init(iterator, NewVCTCallbacksList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        PluginVCTProcessor_t callback = ListIterator_Current(iterator);
        callback(newVCT);
    }

    dvbpsi_atsc_DeleteVCT(newVCT);
        
}

static void DumpDescriptor(char *prefix, dvbpsi_descriptor_t *descriptor)
{
    int i;
    char line[(16 * 3) + 1];
    line[0] = 0;
    LogModule(LOG_DEBUG, PSIPPROCESSOR, "%sTag : 0x%02x (Length %d)\n", prefix, descriptor->i_tag, descriptor->i_length);
    for (i = 0; i < descriptor->i_length; i ++)
    {
        if (i && ((i % 16) == 0))
        {
            LogModule(LOG_DEBUG, PSIPPROCESSOR, "%s%s\n", prefix, line);
            line[0] = 0;
        }
        sprintf(line + strlen(line), "%02x ", descriptor->p_data[i]);
    }
    if (line[0])
    {
        LogModule(LOG_DEBUG, PSIPPROCESSOR, "%s%s\n", prefix, line);
    }
}