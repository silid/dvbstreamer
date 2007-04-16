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


void dvbpsi_DecodeMJDUTC(char *p_mjdutc, dvbpsi_date_time_t *p_date_time)
{
    #define BCD_CHAR_TO_INT(_bcd) (((_bcd >> 4) * 10) + (_bcd & 0x0f))

    uint16_t i_mjd = (((uint16_t)p_mjdutc[0] << 8) | (uint16_t)(p_mjdutc[1] & 0xff));
    double d_mjd = (double)i_mjd;
    /*
        To find Y, M, D from MJD
        Y' = int [ (MJD - 15 078,2) / 365,25 ]
        M' = int { [ MJD - 14 956,1 - int (Y' × 365,25) ] / 30,6001 }
        D = MJD - 14 956 - int (Y' × 365,25) - int (M' × 30,6001)
        If M' = 14 or M' = 15, then K = 1; else K = 0
        Y = Y' + K
        M = M' - 1 - K × 12
    */
    int i_temp_y, i_temp_m, i_k = 0;
    i_temp_y = (int) ((d_mjd - 15078.2)/ 365.25);
    i_temp_m = (int) (((d_mjd - 14956.1) - ((double)i_temp_y * 365.25)) / 30.6001);

    if ((i_temp_m == 14) || (i_temp_m == 15))
    {
        i_k = 1;
    }

    p_date_time->i_year = i_temp_y + i_k + 1900;
    p_date_time->i_month = (i_temp_m - 1) - (i_k * 12);
    p_date_time->i_day = (((i_mjd - 14956) - (int)((double)i_temp_y * 365.25)) - (int)((double)i_temp_m * 30.6001));
    p_date_time->i_hour = BCD_CHAR_TO_INT(p_mjdutc[2]);
    p_date_time->i_minute = BCD_CHAR_TO_INT(p_mjdutc[3]);
    p_date_time->i_second = BCD_CHAR_TO_INT(p_mjdutc[4]);
}

