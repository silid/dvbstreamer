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

epgtypes.c

Defines the types used by the EPG.

*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <limits.h>
#include <time.h>

#include "main.h"
#include "types.h"

#include "epgtypes.h"
             

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void EPGEventRatingDestructor(void *arg);
static void EPGEventDetailDestructor(void *arg);

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
int EPGTypesInit(void)
{
    ObjectRegisterType(EPGEvent_t);
    ObjectRegisterTypeDestructor(EPGEventRating_t, EPGEventRatingDestructor);    
    ObjectRegisterTypeDestructor(EPGEventDetail_t, EPGEventDetailDestructor);
    return 0;
}


int EPGTypesDeInit()
{
    return 0;
}


/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/
static void EPGEventRatingDestructor(void *arg)
{
    EPGEventRating_t *rating = arg;
    free(rating->rating);
    free(rating->system);
}

static void EPGEventDetailDestructor(void *arg)
{
    EPGEventDetail_t *detail = arg;
    free(detail->name);
    free(detail->value);
}
