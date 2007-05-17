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
#include <unistd.h>
#include <stdint.h>
#include <assert.h>

#include <dvbpsi/dvbpsi.h>
#include <dvbpsi/descriptor.h>
#include <dvbpsi/dr.h>
#include <dvbpsi/demux.h>
#include "dvbpsi/mgt.h"
#include "dvbpsi/stt.h"

#include "multiplexes.h"
#include "services.h"
#include "pids.h"
#include "dvb.h"
#include "ts.h"
#include "main.h"
#include "cache.h"
#include "logging.h"
#include "list.h"
#include "subtableprocessor.h"
#include "psipprocessor.h"

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void SubTableHandler(void * state, dvbpsi_handle demuxHandle, uint8_t tableId, uint16_t extension);
static void ProcessMGT(void *arg, dvbpsi_mgt_t *newMGT);
static void ProcessSTT(void *arg, dvbpsi_stt_t *newSTT);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static const char PSIPPROCESSOR[] = "PSIPProcessor";

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/

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
        case 0xC8:
            break;
        /* CVCT */
        case 0xC9:
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
    LogModule(LOG_DEBUG, PSIPPROCESSOR,"New MGT Received! Version %d Protocol %d\n", newMGT->i_version, newMGT->i_protocol);
    for (table = newMGT->p_first_table; table; table = table->p_next)
    {
        LogModule(LOG_DEBUG, PSIPPROCESSOR, "\tType=%d PID=%d Version=%d number bytes=%d\n",
            table->i_type, table->i_pid, table->i_version, table->i_number_bytes);
    }
    dvbpsi_DeleteMGT(newMGT);
}

static void ProcessSTT(void *arg, dvbpsi_stt_t *newSTT)
{
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
    
}

