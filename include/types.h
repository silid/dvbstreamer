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

types.h

Generic type definitions.

*/

#ifndef _DVBSTREAMER_TYPES_H
#define _DVBSTREAMER_TYPES_H
/**
 * @defgroup GlobalTypes Global types
 *@{
 */

/**
 * 2nd part of the 2 stage macro (TOSTRING) to turn an unquoted identifier into a string.
 * @param x identifer to turn into a string.
 */
#define STRINGIFY(x) #x

/**
 * Macro to turn an unquoted identifier into a string.
 * @param x identifer to turn into a string.
 */
#define TOSTRING(x) STRINGIFY(x)

/**
 * True value.
 */
#define TRUE  1
/**
 * False value.
 */
#define FALSE 0

/**
 * Boolean type
 */
#include <stdbool.h>

/** @} */
#endif
