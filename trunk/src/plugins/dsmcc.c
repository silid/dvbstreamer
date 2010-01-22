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

dsmcc.c

Plugin to download DSM-CC data.

*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <pthread.h>

#include "plugin.h"
#include "main.h"
#include "list.h"
#include "logging.h"
#include "servicefilter.h"
#include "ts.h"
#include "events.h"


/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/

/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/
typedef struct DSMCCSession_s
{
    ServiceFilter_t filter;
}DSMCCSession_t;

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void Install(bool installed);
static void CommandEnableDSMCC(int argc, char **argv);
static void CommandDisableDSMCC(int argc, char **argv);
static void CommandDSMCCInfo(int argc, char **argv);

static void HandleServiceFilterRemoved(void *arg, Event_t event, void *payload);
static void HandleServiceFilterChanged(void *arg, Event_t event, void *payload);

void EnableSession(DSMCCSession_t *session);
void DisableSession(DSMCCSession_t *session);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static char DSMCC[]="DSMCC";
/*
static char dsmccSectionFilterType[] = "DSMCC";
*/
static List_t *sessions = NULL;

/*******************************************************************************
* Plugin Setup                                                                 *
*******************************************************************************/

PLUGIN_COMMANDS(
    {
        "enabledsmcc",
        1, 1,
        "Enable DSM-CC data download for the specified service filter.",
        "enabledsmcc <service filter name>\n"
        "Enable DSM-CC data download for the specified service filter.",
        CommandEnableDSMCC
    },
    {
        "disabledsmcc",
        1, 1,
        "Disable DSM-CC data download for the specified service filter.",
        "disabledsmcc <service filter name>\n"
        "Disable DSM-CC data download for the specified service filter.",
        CommandDisableDSMCC
    },
    {
        "dsmccinfo",
        1, 1,
        "Display DSM-CC info for the specified service filter.",
        "dsmccinfo <service filter name>\n"
        "Display DSM-CC info for the specified service filter.",
        CommandDSMCCInfo
    }
);

PLUGIN_FEATURES(
    PLUGIN_FEATURE_INSTALL(Install)
    );

PLUGIN_INTERFACE_CF(
    PLUGIN_FOR_ALL,
    "DSMCC", "0.1",
    "Plugin to allow DSM-CC download.",
    "charrea6@users.sourceforge.net"
    );


/*******************************************************************************
* Global Functions                                                             *
*******************************************************************************/
static void Install(bool installed)
{
    Event_t removedEvent= EventsFindEvent("servicefilter.removed");
    Event_t changedEvent= EventsFindEvent("servicefilter.servicechanged");
    if (installed)
    {
        ObjectRegisterType(DSMCCSession_t);
        sessions = ObjectListCreate();
        EventsRegisterEventListener(removedEvent, HandleServiceFilterRemoved, NULL);
        EventsRegisterEventListener(changedEvent, HandleServiceFilterChanged, NULL);        
    }
    else
    {
        EventsUnregisterEventListener(removedEvent, HandleServiceFilterRemoved, NULL);
        EventsUnregisterEventListener(changedEvent, HandleServiceFilterChanged, NULL);        
        ObjectListFree(sessions);
    }
}

/*******************************************************************************
* Command Functions                                                            *
*******************************************************************************/
static void CommandEnableDSMCC(int argc, char **argv)
{
    ListIterator_t iterator;
    TSReader_t *reader = MainTSReaderGet();
    ServiceFilter_t filter = ServiceFilterFindFilter(reader, argv[0]);
    DSMCCSession_t *session;
    if (filter == NULL)
    {
        CommandError(COMMAND_ERROR_GENERIC, "Failed to find service filter");
        return;
    }
    ListIterator_ForEach(iterator, sessions)
    {
        session = ListIterator_Current(iterator);
        if (session->filter == filter)
        {
            /* We are already downloading DSMCC data for this filter. */
            return;
        }
    }
    session = ObjectCreateType(DSMCCSession_t);
    session->filter = filter;
    EnableSession(session);
    ListAdd(sessions, session);
}

static void CommandDisableDSMCC(int argc, char **argv)
{
    ListIterator_t iterator;
    TSReader_t *reader = MainTSReaderGet();
    ServiceFilter_t filter = ServiceFilterFindFilter(reader, argv[0]);
    DSMCCSession_t *session;
    if (filter == NULL)
    {
        CommandError(COMMAND_ERROR_GENERIC, "Failed to find service filter");
        return;
    }
    ListIterator_ForEach(iterator, sessions)
    {
        session = ListIterator_Current(iterator);
        if (session->filter == filter)
        {
            DisableSession(session);
            ListRemoveCurrent(&iterator);
            break;
        }
    }
}

static void CommandDSMCCInfo(int argc, char **argv)
{
}

static void HandleServiceFilterRemoved(void *arg, Event_t event, void *payload)
{
    ListIterator_t iterator;
    ServiceFilter_t filter = payload;
    DSMCCSession_t *session;
    ListIterator_ForEach(iterator, sessions)
    {
        session = ListIterator_Current(iterator);
        if (session->filter == filter)
        {
            LogModule(LOG_DEBUG, DSMCC, "Removing DSMCC session for service filter %s", ServiceFilterNameGet(filter));
            DisableSession(session);
            ListRemoveCurrent(&iterator);
            break;
        }
    }
}

static void HandleServiceFilterChanged(void *arg, Event_t event, void *payload)
{
    ListIterator_t iterator;
    ServiceFilter_t filter = payload;
    DSMCCSession_t *session;
    ListIterator_ForEach(iterator, sessions)
    {
        session = ListIterator_Current(iterator);
        if (session->filter == filter)
        {
            LogModule(LOG_DEBUG, DSMCC, "Re-enabling DSMCC session for service filter %s", ServiceFilterNameGet(filter));
            EnableSession(session);
            break;
        }
    }
}

void EnableSession(DSMCCSession_t *session)
{
}

void DisableSession(DSMCCSession_t *session)
{
    ObjectRefDec(session);
}

