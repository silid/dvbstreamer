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

datetime.c

Example plugin to print out the date/time from the TDT.

*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdint.h>

#include "logging.h"
#include "plugin.h"
#include "dvbpsi/datetime.h"
#include "dvbpsi/tdttot.h"

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void ProcessTDT(dvbpsi_tdt_tot_t *tdt);
static void CommandDateTime(int argc, char **argv);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static dvbpsi_tdt_tot_t lastDateTime;
static time_t lastReceived = 0;
/*******************************************************************************
* Plugin Setup                                                                 *
*******************************************************************************/
PLUGIN_FEATURES(
    PLUGIN_FEATURE_TDTPROCESSOR(ProcessTDT)
);

PLUGIN_COMMANDS(
    {
        "date",
        FALSE, 0, 0,
        "Display the last date/time received.",
        "Display the last date/time received.",
        CommandDateTime
    }
);

PLUGIN_INTERFACE_CF(
    "Date/Time", 
    "0.1", 
    "Example plugin that uses the TDT/TOT.", 
    "charrea6@users.sourceforge.net"
);

/*******************************************************************************
* NIT Processing Function                                                      *
*******************************************************************************/
static void ProcessTDT(dvbpsi_tdt_tot_t *tdt)
{
    lastDateTime = *tdt;
    /*
    Simple structure copy won't copy the descriptors and these will be free'd
    when the call back returns 
    */
    lastDateTime.p_first_descriptor = NULL; 
    lastReceived = time(NULL);
}

/*******************************************************************************
* Command Functions                                                            *
*******************************************************************************/
static void CommandDateTime(int argc, char **argv)
{
    if (lastReceived)
    {
        CommandPrintf("UTC Date/Time (YYYY/MM/DD hh:mm:ss) %4d/%2d/%2d %02d:%02d:%02d",
            lastDateTime.t_date_time.i_year, lastDateTime.t_date_time.i_month, lastDateTime.t_date_time.i_day,
            lastDateTime.t_date_time.i_hour, lastDateTime.t_date_time.i_minute, lastDateTime.t_date_time.i_second);
        
        CommandPrintf("Last received %d seconds ago.\n", time(NULL) - lastReceived);
    }
    else
    {
        CommandPrintf("No date/time has been received!\n");
    }
}
