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

lcnquery.c

Logical Channel Number Query Plugin.

*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdint.h>

#include "logging.h"
#include "plugin.h"
#include "dbase.h"
#include "tuning.h"
#include "services.h"
#include "multiplexes.h"
#include "dvbpsi/nit.h"
#include "dvbpsi/dr_83.h"


/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/
/* Database entry names */
#define LCNENTRIES_TABLE   "LCNEntries"
#define LCNENTRY_NUMBER    "number"
#define LCNENTRY_ONETID    "networkId"
#define LCNENTRY_TSID      "tsId"
#define LCNENTRY_SERVICEID "serviceId"
#define LCNENTRY_VISIBLE   "visible"

#define LCNENTRY_FIELDS LCNENTRY_NUMBER "," \
                        LCNENTRY_ONETID "," \
                        LCNENTRY_TSID   "," \
                        LCNENTRY_SERVICEID "," \
                        LCNENTRY_VISIBLE

#define MAX_ENTRIES 999


/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/
/* LCN Entry structure */
#define ONETID_INVALID 0

typedef struct LCNEntry_s
{
    uint16_t networkId;
    uint16_t tsId;
    uint16_t serviceId;
    bool     visible;
}LCNEntry_t;


/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void ProcessNIT(dvbpsi_nit_t *nit);
static void LCNQueryInstalled(bool installed );
static void CommandListLCN(int argc, char **argv);
static void CommandFindLCN(int argc, char **argv);
static void CommandSelectLCN(int argc, char **argv);
static LCNEntry_t *GetEntry(int lcn);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static LCNEntry_t entries[MAX_ENTRIES];

/*******************************************************************************
* Plugin Setup                                                                 *
*******************************************************************************/
PLUGIN_FEATURES(
    PLUGIN_FEATURE_NITPROCESSOR(ProcessNIT),
    PLUGIN_FEATURE_INSTALL(LCNQueryInstalled)
);

PLUGIN_COMMANDS(
    {
        "lslcn",
        FALSE, 0, 0,
        "List the logical channel numbers to services.",
        "List all the logical channel numbers and the services they refer to.",
        CommandListLCN
    },
    {
        "findlcn",
        TRUE, 1, 1,
        "Find the service for a logical channel number.",
        "Given a logical channel number return the service name it refers to.",
        CommandFindLCN
    },
    {
        "selectlcn",
        TRUE, 1, 1,
        "Select the service from a logical channel number.",
        "Select service refered to by the logical channel number as the primary service.",
        CommandSelectLCN
    } 
);
#ifdef __CYGWIN__
#define PluginInterface LCNQueryPluginInterface
#endif

PLUGIN_INTERFACE_CF(
    PLUGIN_FOR_DVB,
    "LCNQuery", 
    "0.2", 
    "Logical Channel Number look-up/list", 
    "charrea6@users.sourceforge.net"
);


/*******************************************************************************
* Plugin Install Function                                                      *
*******************************************************************************/
static void LCNQueryInstalled(bool installed)
{
    STATEMENT_INIT;

    if (installed)
    {
        int i;
        for (i = 0; i < MAX_ENTRIES; i++)
        {
            entries[i].networkId = ONETID_INVALID;
            entries[i].visible = FALSE;
        }
        // Load from the database
        sqlite3_exec(DBaseConnectionGet(), "CREATE TABLE " LCNENTRIES_TABLE "("
                     LCNENTRY_NUMBER " PRIMARY KEY,"
                     LCNENTRY_ONETID ","
                     LCNENTRY_TSID   ","
                     LCNENTRY_SERVICEID ","
                     LCNENTRY_VISIBLE ");", NULL, NULL, NULL);

         STATEMENT_PREPARE("SELECT " LCNENTRY_FIELDS " FROM " LCNENTRIES_TABLE ";");

         if (rc == SQLITE_OK)
         {
            bool more = TRUE;
            while (more)
            {
                STATEMENT_STEP();
                if (rc == SQLITE_ROW)
                {
                    LCNEntry_t *entry;
                    int lcn = STATEMENT_COLUMN_INT(0);
                    entry = GetEntry(lcn);

                    if (entry)
                    {
                        entry->networkId   = STATEMENT_COLUMN_INT(1);
                        entry->tsId     = STATEMENT_COLUMN_INT(2);
                        entry->serviceId= STATEMENT_COLUMN_INT(3);
                        entry->visible  = STATEMENT_COLUMN_INT(4);
                    }
                }
                else
                {
                    more = FALSE;
                }
            } 
            
         }
         STATEMENT_FINALIZE();
    }
    else
    {
        int i;
        // Store to the database
        sqlite3_exec(DBaseConnectionGet(), "BEGIN TRANSACTION;", NULL, NULL, NULL);
        
        sqlite3_exec(DBaseConnectionGet(), "DELETE * FROM " LCNENTRIES_TABLE ";", NULL, NULL, NULL);

        for (i = 0; i < MAX_ENTRIES; i ++)
        {
            if (entries[i].networkId != ONETID_INVALID)
            {
                STATEMENT_PREPAREVA("INSERT INTO " LCNENTRIES_TABLE " VALUES (%d,%d,%d,%d,%d);", i + 1, 
                    entries[i].networkId, entries[i].tsId, entries[i].serviceId, entries[i].visible);
                STATEMENT_STEP();
                STATEMENT_FINALIZE();
            }
        }

        sqlite3_exec(DBaseConnectionGet(), "COMMIT TRANSACTION;", NULL, NULL, NULL);
    }
    
}

/*******************************************************************************
* NIT Processing Function                                                      *
*******************************************************************************/
static void ProcessNIT(dvbpsi_nit_t *nit)
{
    dvbpsi_nit_transport_t *transport = nit->p_first_transport;

    for (transport= nit->p_first_transport; transport; transport = transport->p_next)
    {
        dvbpsi_descriptor_t *descriptor;

        for (descriptor = transport->p_first_descriptor; descriptor; descriptor = descriptor->p_next)
        {
            if (descriptor->i_tag == 0x83)
            {
                dvbpsi_lcn_dr_t * lcn_descriptor = dvbpsi_DecodeLCNDr(descriptor);
                if (lcn_descriptor)
                {
                    int i;

                    for (i = 0; i < lcn_descriptor->i_number_of_entries; i ++)
                    {
                        LCNEntry_t *entry;
                        entry = GetEntry(lcn_descriptor->p_entries[i].i_logical_channel_number);
                        if (entry)
                        {
                            if (!entry->visible || lcn_descriptor->p_entries[i].b_visible_service_flag)
                            {
                                entry->networkId = transport->i_original_network_id;
                                entry->tsId = transport->i_ts_id;
                                entry->serviceId = lcn_descriptor->p_entries[i].i_service_id;
                                entry->visible = lcn_descriptor->p_entries[i].b_visible_service_flag;
                            }
                        }

                    }
                }
            }
        }
    }
}

/*******************************************************************************
* Command Functions                                                            *
*******************************************************************************/
static void CommandListLCN(int argc, char **argv)
{
    int i;
    int count = 0;

    for ( i = 0; i < MAX_ENTRIES; i ++)
    {
        if (entries[i].networkId != ONETID_INVALID)
        {
            Service_t *service = ServiceFindFQID(entries[i].networkId, entries[i].tsId, entries[i].serviceId);
            if (service)
            {
                if (entries[i].visible)
                {
                    CommandPrintf("%4d : %s\n", i + 1, service->name);
                    count ++;
                }
                ServiceRefDec(service);

            }
        }
    }
    CommandError(COMMAND_OK, "%d channels found", count);
}

static void CommandFindLCN(int argc, char **argv)
{
    int lcn = atoi(argv[0]);
    Service_t *service;
    LCNEntry_t *entry;
    if (lcn == 0)
    {
        CommandError(COMMAND_ERROR_WRONG_ARGS, "Unknown Logical Channel Number.");
        return;
    }
    entry = GetEntry(lcn);
    if (entry->networkId == ONETID_INVALID)
    {
        CommandError(COMMAND_ERROR_GENERIC, "No such Logical Channel Number.");
        return;
    }

    service = ServiceFindFQID(entry->networkId, entry->tsId, entry->serviceId);
    if (service)
    {
        CommandPrintf("%s\n", service->name);
        ServiceRefDec(service);
    }
    
}

static void CommandSelectLCN(int argc, char **argv)
{
    int lcn = atoi(argv[0]);
    Service_t *service;
    LCNEntry_t *entry;
    if (lcn == 0)
    {
        CommandError(COMMAND_ERROR_WRONG_ARGS, "Unknown Logical Channel Number.");
        return;
    }
    entry = GetEntry(lcn);
    if (entry->networkId == ONETID_INVALID)
    {
        CommandError(COMMAND_ERROR_GENERIC, "No such Logical Channel Number.");
        return;
    }

    service = ServiceFindFQID(entry->networkId, entry->tsId, entry->serviceId);
    if (service)
    {
        TuningCurrentServiceSet(service);
        CommandPrintf("%04x.%04x.%04x : \"%s\"\n", entry->networkId, entry->tsId, entry->serviceId, service->name);
        ServiceRefDec(service);
    }
    else
    {
        CommandError(COMMAND_ERROR_GENERIC, "Failed to find service!");        
    }
}
static LCNEntry_t *GetEntry(int lcn)
{
    return &entries[lcn - 1];
}

