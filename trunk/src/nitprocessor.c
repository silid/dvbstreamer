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

/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/


/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/

static void SubTableHandler(void * state, dvbpsi_handle demuxHandle, uint8_t tableId, uint16_t extension);
static void NITHandler(void* arg, dvbpsi_nit_t* newNIT);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/

static List_t *NewNITCallbacksList = NULL;

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
int NITProcessorInit(void)
{
    NewNITCallbacksList = ListCreate();
    return NewNITCallbacksList ? 0: -1;
}

void NITProcessorDeInit(void)
{
    ListFree(NewNITCallbacksList, NULL);
}

PIDFilter_t *NITProcessorCreate(TSFilter_t *tsfilter)
{
    PIDFilter_t *result = SubTableProcessorCreate(tsfilter, PID_NIT, SubTableHandler, NULL, NULL, NULL);
    if (result)
    {
        result->name = "NIT";
        result->type = PSISIPIDFilterType;
        if (tsfilter->adapter->hardwareRestricted)
        {
            DVBDemuxAllocateFilter(tsfilter->adapter, PID_NIT, TRUE);
        }        
    }

    return result;
}

void NITProcessorDestroy(PIDFilter_t *filter)
{
    if (filter->tsFilter->adapter->hardwareRestricted)
    {
        DVBDemuxReleaseFilter(filter->tsFilter->adapter, PID_NIT);
    }    
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

/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/

static void SubTableHandler(void * arg, dvbpsi_handle demuxHandle, uint8_t tableId, uint16_t extension)
{
    if((tableId == TABLE_ID_NIT_ACTUAL) || (tableId == TABLE_ID_NIT_OTHER))
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
    ObjectRefDec(newNIT);
}

