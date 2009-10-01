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

mpeg2.c

Initialise/Deinitialise the MPEG2 Standard.

*/
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

#include "events.h"
#include "multiplexes.h"
#include "dvbadapter.h"
#include "ts.h"
#include "standard/mpeg2.h"
#include "patprocessor.h"
#include "pmtprocessor.h"

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
EventSource_t MPEG2EventSource = NULL;
char MPEG2FilterType[]="MPEG2";
static PATProcessor_t patProcessor;
static PMTProcessor_t pmtProcessor;

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/

int MPEG2StandardInit(TSReader_t *reader)
{
    if (MPEG2EventSource == NULL)
    {
        MPEG2EventSource = EventsRegisterSource("mpeg2");
    }
    patProcessor = PATProcessorCreate(reader);
    if (patProcessor == NULL)
    {
        return -1;
    }
    pmtProcessor = PMTProcessorCreate(reader);
    if (pmtProcessor == NULL)
    {
        PATProcessorDestroy(patProcessor);
        return -1;
    }
    return 0;
}

int MPEG2StandardDeinit(TSReader_t *reader)
{
    reader = reader;
    PATProcessorDestroy(patProcessor);
    PMTProcessorDestroy(pmtProcessor);
    return 0;
}
