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

dvbtext.h

DVB Text Conversion functions.

*/
#ifndef _DVBTEXT_H
#define _DVBTEXT_H
#include <sys/types.h>

/**
 * @defgroup DVBText Functions to convert DVB encoded strings to UTF-8.
 * @{
 */

/**
 * Converts the supplied string to UTF-8. The input string should be in DVB 
 * format (see ETSI EN 300 468 Annex A2).
 * The returned pointer is to a static buffer that will be reused by future calls
 * to this method so the calling function should copy out the contents before calling 
 * this function again.
 *
 * @param toConvert The string to convert to UTF-8.
 * @param toConvertLen The length of the string to convert.
 * @return A pointer to a buffer containing the input string in UTF-8 format, or 
 *         NULL if the conversion failed.
 */
char *DVBTextToUTF8(char *toConvert, size_t toCovertLen);
/** @} */
#endif

