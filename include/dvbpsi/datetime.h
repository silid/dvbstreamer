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
#ifndef _DATETIME_H
#define _DATETIME_H
#include <time.h>

/*****************************************************************************
 * dvbpsi_DecodeMJDUTC
 *****************************************************************************/
/*!
 * \fn void dvbpsi_DecodeMJDUTC(uint8_t *p_mjdutc, 
                                struct tm *p_date_time)
 * \brief Decode date/time encoded in MJD UTC format into its constituent parts.
 * \param p_mjdutc pointer to MJD UTC encoded date time.
 * \param p_date_time pointer to a tm structure to hold the converted date.
 */
void dvbpsi_DecodeMJDUTC(uint8_t *p_mjdutc, struct tm *p_date_time);

#endif /*_DATETIME_H*/
