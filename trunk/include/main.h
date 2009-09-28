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

main.h

Entry point to the application.

*/
#ifndef _MAIN_H
#define _MAIN_H

#include "types.h"
#include "dvbadapter.h"
#include "ts.h"
#include "services.h"
#include "multiplexes.h"
#include "plugin.h"

/**
 * Boolean used to signal the program to terminate.
 */
extern bool ExitProgram;

/**
 * Boolean indicating whether DVBStreamer is in daemon mode.
 */
extern bool DaemonMode;

/**
 * Constant for the PrimaryService output name
 */
extern const char PrimaryService[];

/**
 * Directory path where DVBStreamer stores its data.
 */
extern char DataDirectory[];

/**
 * Writes any changes in the cache back to the database, ensuring the TS Filter is disabled.
 */
void UpdateDatabase();

/**
 * Retrieve the main Transport Stream Filter object.
 * @return The main TSReader_t object.
 */
TSReader_t *MainTSReaderGet(void);

/**
 * Retrieve the DVBAdapter_t object being used by the main TSReader_t object.
 * @return The main DVBAdapter_t object.
 */
DVBAdapter_t *MainDVBAdapterGet(void);

/**
 * Used to determine whether DVBStreamer is using a DVB frontend.
 * @return TRUE if the frontend is a DVB frontend, FALSE if it is an ATSC frontend.
 */
bool MainIsDVB();
#endif
