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
    /* STT */
    /* MGT */
    /* TVCT */
    /* CVCT */
}


