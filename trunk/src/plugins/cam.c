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
#include <stdbool.h>

#include "logging.h"
#include "plugin.h"
#include "dvbpsi/pmt.h"
#include "dispatchers.h"
#include "properties.h"
#include "servicefilter.h"
#include "main.h"

#include <linux/dvb/ca.h>
#include "en50221.h"

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void Install(bool installed);
static void ProcessPMT(dvbpsi_pmt_t *pmt);
static bool PMTDoesNeedDecrypting(dvbpsi_pmt_t *pmt);
/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static char CAM[] = "CAM";
static ev_timer pollTimer;
static ServiceFilter_t primaryFilter = NULL;
static dvbpsi_pmt_t *currentPMT = NULL;
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
* CAM Processing Function                                                      *
*******************************************************************************/
void demux_ResendCAPMTs(void)
{
    /* Resend PMTs */
    if (currentPMT)
    {
        en50221_AddPMT(currentPMT);
    }
}

/*******************************************************************************
* PMT Processing Function                                                      *
*******************************************************************************/
static void ProcessPMT(dvbpsi_pmt_t *pmt)
{
    bool was_decrypting = false;
    bool needs_decrypting = false;
    Service_t *service;
    
    if (!primaryFilter)
    {
        return;
    }
    
    service = ServiceFilterServiceGet(primaryFilter);
    
    if (pmt->i_program_number == service->id)
    {
        needs_decrypting = PMTDoesNeedDecypting(pmt);
        
        if (currentPMT)
        {
            if (needs_decrypting)
            {
                en50221_UpdatePMT(pmt);
            }
            else
            {
                en50221_DeletePMT(currentPMT);
            }
            ObjectRefDec(currentPMT);
            currentPMT = NULL;        
        }
        else
        {
            if (needs_decrypting)
            {
                en50221_AddPMT(currentPMT);
            }
        }
       
        if (needs_decrypting)
        {
            currentPMT = pmt;
            ObjectRefInc(pmt);
        }
    }

}

static void camPollTimer(struct ev_loop *loop, ev_timer *w, int revents)
{
    en50221_Poll();
}


/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/
static void Install(bool installed)
{

    if (installed)
    {
        PropertyValue_t adapterProp;

        LogModule(LOG_INFO, CAM, "Installing");
        /* Register for service filter events */
        if (PropertiesGet("adapter.number", &adapterProp))
        {
            LogModule(LOG_ERROR, CAM, "Failed to get adapter number!");
        }
        else
        {
            ev_tstamp timeout = (ev_tstamp)i_ca_timeout / 1000000.0;
            en50221_Init(adapterProp.u.integer);
            ev_timer_init(&pollTimer, camPollTimer, timeout, timeout);
            ev_timer_start(DispatchersGetInput(), &pollTimer);
            /* Only bother to retrieve the primary filter if we have a CAM */
            if (i_ca_handle)
            {
                primaryFilter = MainServiceFilterGetPrimary();
            }
        }
    }
    else
    {
        /* Unregister service filter events */
        LogModule(LOG_INFO, CAM, "Uninstalling");
        ev_timer_stop(DispatchersGetInput(), &pollTimer);
        en50221_Reset();
    }
}


static bool PMTDoesNeedDecrypting(dvbpsi_pmt_t *pmt)
{
    dvbpsi_descriptor_t *desc;
    dvbpsi_pmt_es_t *es;
    for(desc = pmt->p_first_descriptor; desc; desc = desc->p_next)
    {
        if (desc->i_tag == 9)
        {
            return true;
        }
    }
    for (es = pmt->p_first_es; es; es = es->p_next)
    {
        for(desc = es->p_first_descriptor; desc; desc = desc->p_next)
        {
            if (desc->i_tag == 9)
            {
                return true;
            }
        }
    }

    return false;
}