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

sicapture.c

Plugin to capture only PSI/SI information to an MRL.

*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "main.h"
#include "plugin.h"
#include "multiplexes.h"
#include "services.h"
#include "tuning.h"
#include "cache.h"
#include "logging.h"
#include "deliverymethod.h"

/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/
#define MAX_EITS 128 /* Maximum number of EIT tables (PIDs) */
#define MAX_ETTS 128 /* Maximum number of ETT tables (PIDs) */

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void InitFilter(PIDFilter_t *filter);
static void DeinitFilter(PIDFilter_t *filter);
static void NewMGT(dvbpsi_atsc_mgt_t *newMGT);
static int FilterPacket(PIDFilter_t *pidfilter, void *arg, uint16_t pid, TSPacket_t *packet);

static void CommandEnableSICapture(int argc, char **argv);
static void CommandDisableSICapture(int argc, char **argv);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static PluginFilter_t CaptureFilter = {NULL, InitFilter, DeinitFilter};
static const char SICAPTURE[] = "SICapture";

static int EventInfoTableCount = 0;
static uint16_t EventInfoTablePIDs[MAX_EITS];

static int ExtendedTextTableCount = 0;
static uint16_t ExtendedTextTablePIDs[MAX_ETTS];

static uint16_t channelETT = 0;

/*******************************************************************************
* Plugin Setup                                                                 *
*******************************************************************************/
#ifdef __CYGWIN__
#define PluginInterface SICapturePluginInterface
#endif

PLUGIN_FEATURES(
    PLUGIN_FEATURE_FILTER(CaptureFilter),
    PLUGIN_FEATURE_MGTPROCESSOR(NewMGT)
    );

PLUGIN_COMMANDS(
    {
        "enablesicap",
        TRUE, 1, 1,
        "Enable the capture of PSI/SI data.",
        "enablesicap <mrl>\n"
        "Enables and sets the MRL to send captured PSI/SI packets to.",
        CommandEnableSICapture
    },
    {
        "disablesicap",
        TRUE, 0, 0,
        "Disable the capture of PSI/SI data.",
        "enablesicap <mrl>\n"
        "Disables the capture of PSI/SI packets.",
        CommandDisableSICapture
    }
    );

PLUGIN_INTERFACE_CF(
    PLUGIN_FOR_ALL,
    "SICapture", "0.1", 
    "Plugin to capture PSI/SI to an MRL.", 
    "charrea6@users.sourceforge.net"
    );
/*******************************************************************************
* Filter Functions                                                             *
*******************************************************************************/
static void InitFilter(PIDFilter_t *filter)
{
    filter->name = "PSI/SI Capture";

    PIDFilterFilterPacketSet(filter, FilterPacket, NULL);   
}

static void DeinitFilter(PIDFilter_t *filter)
{
    filter->enabled = FALSE;
}

static void NewMGT(dvbpsi_atsc_mgt_t *newMGT)
{
    dvbpsi_atsc_mgt_table_t * table;
    EventInfoTableCount = 0;
    ExtendedTextTableCount = 0;
    channelETT = 0;
    for (table = newMGT->p_first_table; table; table = table->p_next)
    {
        if (table->i_type == 0x004)
        {
            channelETT = table->i_pid;
            LogModule(LOG_DEBUG, SICAPTURE, "Channel ETT (%04x)\n", channelETT);
        }
        if ((table->i_type >= 0x100) && (table->i_type <= 0x17f))
        {
            LogModule(LOG_DEBUG, SICAPTURE, "EIT %d (%04x)\n",table->i_type - 0x100, table->i_pid);
            EventInfoTablePIDs[EventInfoTableCount] = table->i_pid;
            EventInfoTableCount ++;
        }
        if ((table->i_type >= 0x200) && (table->i_type <= 0x27f))
        {
            LogModule(LOG_DEBUG, SICAPTURE, "ETT %d (%04x)\n",table->i_type - 0x200, table->i_pid);
            ExtendedTextTablePIDs[ExtendedTextTableCount] = table->i_pid;
            ExtendedTextTableCount ++;
        }
    }
}

static int FilterPacket(PIDFilter_t *pidfilter, void *arg, uint16_t pid, TSPacket_t *packet)
{
    int result = 0;
    int i;
    Multiplex_t *mux;

    /* Handle PAT */
    if (pid == 0)
    {
        result = 1;
    }

    /* Handle CAT */
    if (pid == 1)
    {
        result = 1;
    }
    
    /* Handle PMTs */
    if(result == 0)
    {
        mux = TuningCurrentMultiplexGet();
        if (mux)
        {
            int i;
            int count;
            Service_t **services;

            services = CacheServicesGet(&count);
            for (i = 0; i < count; i ++)
            {
                if (pid == services[i]->pmtPid)
                {
                    result =  1;
                    break;
                }
            }
            CacheServicesRelease();
            MultiplexRefDec(mux);
        }
    }

    /* Standard specific PIDs */    
    if (result == 0)
    {
        if (MainIsDVB())
        {
            switch(pid)
            {
                case 0x10: /* NIT, ST*/
                case 0x11: /* SDT, BAT, ST*/
                case 0x12: /* EIT, ST, CIT */
                case 0x13: /* RST, ST */
                case 0x14: /* TDT, TOT, ST */
                case 0x16: /* RNT */
                    result = 1;
                    break;
                default:
                    break;
            }
        }
        else
        {
            if (pid == 0x1ffb)
            {
                result = 1;
            }
            else
            {
                if (channelETT && (channelETT == pid))
                {
                    result = 1;
                }
                if (result == 0)
                {
                    for (i = 0; i < EventInfoTableCount; i ++)
                    {
                        if (EventInfoTablePIDs[i] == pid)
                        {
                            result = 1;
                            break;
                        }
                    }
                }
                if (result == 0)
                {
                    for (i = 0; i < ExtendedTextTableCount; i ++)
                    {
                        if (ExtendedTextTablePIDs[i] == pid)
                        {
                            result = 1;
                            break;
                        }
                    }
                }
                
            }
        }
    }
    return result;
}
/*******************************************************************************
* Command Functions                                                            *
*******************************************************************************/
static void CommandEnableSICapture(int argc, char **argv)
{
    if (DeliveryMethodManagerFind(argv[0], CaptureFilter.filter))
    {
        CommandPrintf("SI Capture started (%s)\n", argv[0]);
    }
    else
    {
        CommandPrintf("Failed to find handler for %s\n", argv[0]);
    }
    CaptureFilter.filter->enabled = TRUE;
}

static void CommandDisableSICapture(int argc, char **argv)
{
    CaptureFilter.filter->enabled = FALSE;
    DeliveryMethodManagerFree(CaptureFilter.filter);
    CommandPrintf("SI Capture stopped\n");
}

