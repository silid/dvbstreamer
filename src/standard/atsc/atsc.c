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

atsc.c

Initialise/Deinitialise the ATSC Standard.

*/
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

#include "standard/mpeg2.h"
#include "standard/atsc.h"
#include "psipprocessor.h"
#include "atsctext.h"

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
EventSource_t ATSCEventSource = NULL;
static PSIPProcessor_t psipProcessor;

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/

int ATSCStandardInit(TSReader_t *reader)
{
    if (ATSCEventSource == NULL)
    {
        ATSCEventSource = EventsRegisterSource("atsc");
    }

    if (ATSCMultipleStringsInit())
    {
        return -1;
    }
    
    if (MPEG2StandardInit(reader))
    {
        return -1;
    }
    
    psipProcessor = PSIPProcessorCreate(reader);
    if (psipProcessor == NULL)
    {
        MPEG2StandardDeinit(reader);        
        return -1;
    }
    return 0;
}

int ATSCStandardDeinit(TSReader_t *reader)
{
    MPEG2StandardDeinit(reader);
    PSIPProcessorDestroy(psipProcessor);
    ATSCMultipleStringsDeInit();
    return 0;
}
