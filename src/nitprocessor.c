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

nitprocessor.c

Process Network Information Tables.

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

#include "multiplexes.h"
#include "services.h"
#include "pids.h"
#include "dvb.h"
#include "ts.h"
#include "main.h"
#include "cache.h"
#include "logging.h"
#include "list.h"
#include "nitprocessor.h"
#include "subtableprocessor.h"
#include "dvbpsi/nit.h"


#define TABLE_ID_NIT_ACTUAL 0x40
#define TABLE_ID_NIT_OTHER  0x41

static void SubTableHandler(void * state, dvbpsi_handle demuxHandle, uint8_t tableId, uint16_t extension);
static void NITHandler(void* arg, dvbpsi_nit_t* newNIT);

static List_t *NewNITCallbacksList = NULL;

PIDFilter_t *NITProcessorCreate(TSFilter_t *tsfilter)
{
    PIDFilter_t *result = SubTableProcessorCreate(tsfilter, 0x10, SubTableHandler, NULL, NULL, NULL);
    if (result)
    {
        result->name = "NIT";
    }

    if (!NewNITCallbacksList)
    {
        NewNITCallbacksList = ListCreate();
    }
    
    return result;
}

void NITProcessorDestroy(PIDFilter_t *filter)
{
    SubTableProcessorDestroy(filter);
}

void NITProcessorRegisterNITCallback(PluginNITProcessor_t callback)
{
    if (NewNITCallbacksList)
    {
        ListAdd(NewNITCallbacksList, callback);
    }
}

void NITProcessorUnRegisterNITCallback(PluginNITProcessor_t callback)
{
    if (NewNITCallbacksList)
    {
        ListRemove(NewNITCallbacksList, callback);
    }
}

static void SubTableHandler(void * arg, dvbpsi_handle demuxHandle, uint8_t tableId, uint16_t extension)
{
    if(tableId == TABLE_ID_NIT_ACTUAL)
    {
        dvbpsi_AttachNIT(demuxHandle, tableId, extension, NITHandler, arg);
    }
}

static void NITHandler(void* arg, dvbpsi_nit_t* newNIT)
{
    ListIterator_t iterator;

    for (ListIterator_Init(iterator, NewNITCallbacksList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        PluginNITProcessor_t callback = ListIterator_Current(iterator);
        callback(newNIT);
    }
    dvbpsi_DeleteNIT(newNIT);
}

