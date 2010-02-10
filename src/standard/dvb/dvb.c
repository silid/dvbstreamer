/*
Copyright (C) 2009  Adam Charrett

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

dvb.c

Initialise/Deinitialise the DVB Standard.

*/
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

#include "standard/mpeg2.h"
#include "standard/dvb.h"
#include "sdtprocessor.h"
#include "nitprocessor.h"
#include "tdtprocessor.h"

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
EventSource_t DVBEventSource;
char DVBFilterType[] = "DVB";
static SDTProcessor_t sdtProcessor = NULL;
static NITProcessor_t nitProcessor = NULL;
static TDTProcessor_t tdtProcessor = NULL;

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/

int DVBStandardInit(TSReader_t *reader)
{
    if (DVBEventSource == NULL)
    {
        DVBEventSource = EventsRegisterSource(DVBFilterType);
    }
    if (MPEG2StandardInit(reader))
    {
        return -1;
    }
    sdtProcessor = SDTProcessorCreate(reader);
    if (sdtProcessor == NULL)
    {
        goto failure;
    }
    nitProcessor = NITProcessorCreate(reader);
    if (nitProcessor == NULL)
    {
        goto failure;
    }
    tdtProcessor = TDTProcessorCreate(reader);    
    if (tdtProcessor == NULL)
    {
        goto failure;
    }    
    return 0;
failure:
    MPEG2StandardDeinit(reader);
    if (sdtProcessor)
    {
        SDTProcessorDestroy(sdtProcessor);
    }
    if (nitProcessor)
    {
        NITProcessorDestroy(nitProcessor);
    }
    if (tdtProcessor)
    {
        TDTProcessorDestroy(tdtProcessor);
    }    
    return -1;
}

int DVBStandardDeinit(TSReader_t *reader)
{
    MPEG2StandardDeinit(reader);
    SDTProcessorDestroy(sdtProcessor);
    NITProcessorDestroy(nitProcessor);
    TDTProcessorDestroy(tdtProcessor);
    return 0;
}
