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

typedef struct dvbpsi_date_time_s
{
    int                     i_year;  /*!< Year in UTC */
    int                     i_month; /*!< Month in UTC */
    int                     i_day;   /*!< Day in UTC */
    int                     i_hour;  /*!< Hour in UTC */
    int                     i_minute;/*!< Minute in UTC */
    int                     i_second;/*!< Second in UTC */
}dvbpsi_date_time_t;

/*****************************************************************************
 * dvbpsi_DecodeMJDUTC
 *****************************************************************************/
/*!
 * \fn void dvbpsi_DecodeMJDUTC(char *p_mjdutc, 
                                int *p_year, 
                                int *p_month, 
                                int *p_day, 
                                int *p_hour, 
                                int *p_minute, 
                                int *p_second)
 * \brief Decode date/time encoded in MJD UTC format into its constituent parts.
 * \param p_mjdutc pointer to MJD UTC encoded date time.
 * \param p_year Integer to store the decoded year in.
 * \param p_month Integer to store the decoded month in.
 * \param p_day Integer to store the decoded day in.
 * \param p_hour Integer to store the decoded hour in.
 * \param p_minute Integer to store the decoded minute in.
 * \param p_second Integer to store the decoded second in.
 */
void dvbpsi_DecodeMJDUTC(uint8_t *p_mjdutc, dvbpsi_date_time_t *p_date_time);
#endif /*_DATETIME_H*/
