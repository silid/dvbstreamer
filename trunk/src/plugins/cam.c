/*
Copyright (C) 2010  Adam Charrett

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

cam.c

Plugin to enable use of a CAM to decrypt content.

*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdint.h>

#include "logging.h"
#include "plugin.h"
#include "dvbpsi/pmt.h"


/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void Install(bool installed);
static void ProcessPMT(dvbpsi_pmt_t *pmt);
/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static char CAM[] = "CAM";
/*******************************************************************************
* Plugin Setup                                                                 *
*******************************************************************************/
PLUGIN_FEATURES(
    PLUGIN_FEATURE_INSTALL(Install),
    PLUGIN_FEATURE_PMTPROCESSOR(ProcessPMT)
);

PLUGIN_INTERFACE_F(
    PLUGIN_FOR_ALL,
    "CAM",
    "1.0",
    "Plugin uses a CAM to decrypt broadcast streams.",
    "charrea6@users.sourceforge.net"
);

/*******************************************************************************
* NIT Processing Function                                                      *
*******************************************************************************/
static void ProcessPMT(dvbpsi_pmt_t *pmt)
{
    /* Do nothing */
    pmt = pmt;
}

/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/
static void Install(bool installed)
{
    if (installed)
    {
        LogModule(LOG_INFO, CAM, "Installing");
        /* Register for service filter events */
    }
    else
    {
        /* Unregister service filter events */
        LogModule(LOG_INFO, CAM, "Uninstalling");
    }
}
