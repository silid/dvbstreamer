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

datetime.h

Date and Time decoding functions.

*/

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#if defined(HAVE_INTTYPES_H)
#include <inttypes.h>
#elif defined(HAVE_STDINT_H)
#include <stdint.h>
#endif

#include "dvbpsi.h"
#include "datetime.h"
#include "string.h"

#define MJD_UNIX_EPOCH 40587
#define SECS_PER_MIN 60
#define SECS_PER_HOUR (SECS_PER_MIN * 60)
#define SECS_PER_DAY (SECS_PER_HOUR * 24)

void dvbpsi_DecodeMJDUTC(uint8_t *p_mjdutc, struct tm *p_date_time)
{
    #define BCD_CHAR_TO_INT(_bcd) (((_bcd >> 4) * 10) + (_bcd & 0x0f))
    time_t secs;
    uint16_t i_mjd = (((uint16_t)p_mjdutc[0] << 8) | (uint16_t)(p_mjdutc[1] & 0xff));

    
    secs = (time_t)(((uint32_t)i_mjd - MJD_UNIX_EPOCH) * SECS_PER_DAY);

    secs += (BCD_CHAR_TO_INT(p_mjdutc[2])) * SECS_PER_HOUR+
                (BCD_CHAR_TO_INT(p_mjdutc[3])) * SECS_PER_MIN+
                (BCD_CHAR_TO_INT(p_mjdutc[4]));

    memcpy(p_date_time, gmtime(&secs), sizeof(struct tm));

    
}

