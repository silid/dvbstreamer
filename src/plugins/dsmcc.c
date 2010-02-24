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
#include "dvbpsi/dvbpsi.h"
#include "dvbpsi/descriptor.h"
#include "dvbpsi/dr.h"
#include "dvbpsi/sections.h"
#include "plugin.h"
#include "main.h"
#include "list.h"
#include "logging.h"
#include "servicefilter.h"
#include "ts.h"
#include "tuning.h"
#include "events.h"
#include "pids.h"
#include "cache.h"
#include "libdsmcc/libdsmcc.h"

/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/
#define DSMCC_FILTER_PRIORITY 5
#define INVALID_PID 0xffff

#define TAG_CAROUSEL_ID_DESCRIPTOR     0x13
#define TAG_ASSOCIATION_TAG_DESCRIPTOR 0x14
/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/
struct DSMCCDownloadSession_s;

typedef struct DSMCCPID_s
{
    uint16_t pid;
    uint16_t tag;
    dvbpsi_handle sectionFilter;
    struct DSMCCDownloadSession_s *session;
}DSMCCPID_t;

typedef struct DSMCCDownloadSession_s
{
    Service_t *service;
    List_t *pids;
    TSFilterGroup_t *filterGroup;
    struct dsmcc_status status;
}DSMCCDownloadSession_t;

typedef struct DSMCCSession_s
{
    ServiceFilter_t filter;
    DSMCCDownloadSession_t *downloadSession;
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
static void HandleTuningMultiplexChanged(void *arg, Event_t event, void *payload);

static void EnableSession(DSMCCSession_t *session);

static void SessionDestructor(void *arg);
static void DownloadSessionDestructor(void *arg);
static void DSMCCPIDDestructor(void *arg);

static DSMCCDownloadSession_t *DownloadSessionGet(Service_t *service);

static void DSMCCSectionCallback(void *p_cb_data, dvbpsi_handle h_dvbpsi, dvbpsi_psi_section_t* p_section);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static char DSMCC[]="DSMCC";
/*
static char dsmccSectionFilterType[] = "DSMCC";
*/
static List_t *sessions = NULL;
static List_t *downloadSessions = NULL;
static pthread_mutex_t sessionMutex = PTHREAD_MUTEX_INITIALIZER;
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
    Event_t removedEvent= EventsFindEvent("ServiceFilter.Removed");
    Event_t changedEvent= EventsFindEvent("ServiceFilter.ServiceChanged");
    Event_t muxChangedEvent = EventsFindEvent("Tuning.MultiplexChanged");
    if (installed)
    {
        ObjectRegisterTypeDestructor(DSMCCSession_t, SessionDestructor);
        ObjectRegisterTypeDestructor(DSMCCDownloadSession_t, DownloadSessionDestructor);
        ObjectRegisterTypeDestructor(DSMCCPID_t, DSMCCPIDDestructor);
        sessions = ObjectListCreate();
        downloadSessions = ObjectListCreate();
        EventsRegisterEventListener(removedEvent, HandleServiceFilterRemoved, NULL);
        EventsRegisterEventListener(changedEvent, HandleServiceFilterChanged, NULL);        
        EventsRegisterEventListener(muxChangedEvent, HandleTuningMultiplexChanged, NULL);        
    }
    else
    {
        EventsUnregisterEventListener(removedEvent, HandleServiceFilterRemoved, NULL);
        EventsUnregisterEventListener(changedEvent, HandleServiceFilterChanged, NULL);   
        EventsUnregisterEventListener(muxChangedEvent, HandleTuningMultiplexChanged, NULL);        
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
    bool found = FALSE;
    if (filter == NULL)
    {
        CommandError(COMMAND_ERROR_GENERIC, "Failed to find service filter");
        return;
    }
    pthread_mutex_lock(&sessionMutex);
    ListIterator_ForEach(iterator, sessions)
    {
        session = ListIterator_Current(iterator);
        if (session->filter == filter)
        {
            /* We are already downloading DSMCC data for this filter. */
            found = TRUE;
            break;
        }
    }
    if (!found)
    {
        session = ObjectCreateType(DSMCCSession_t);
        session->filter = filter;
        EnableSession(session);
        ListAdd(sessions, session);
    }
    pthread_mutex_unlock(&sessionMutex);
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
    pthread_mutex_lock(&sessionMutex);    
    ListIterator_ForEach(iterator, sessions)
    {
        session = ListIterator_Current(iterator);
        if (session->filter == filter)
        {
            ListRemoveCurrent(&iterator);
            ObjectRefDec(session);
            break;
        }
    }
    pthread_mutex_unlock(&sessionMutex);    
}

static void CommandDSMCCInfo(int argc, char **argv)
{
}

static void HandleServiceFilterRemoved(void *arg, Event_t event, void *payload)
{
    ListIterator_t iterator;
    ServiceFilter_t filter = payload;
    DSMCCSession_t *session;
    pthread_mutex_lock(&sessionMutex);        
    ListIterator_ForEach(iterator, sessions)
    {
        session = ListIterator_Current(iterator);
        if (session->filter == filter)
        {
            LogModule(LOG_DEBUG, DSMCC, "Removing DSMCC session for service filter %s", ServiceFilterNameGet(filter));
            ObjectRefDec(session);
            ListRemoveCurrent(&iterator);
            break;
        }
    }
    pthread_mutex_unlock(&sessionMutex);        
}

static void HandleServiceFilterChanged(void *arg, Event_t event, void *payload)
{
    ListIterator_t iterator;
    ServiceFilter_t filter = payload;
    DSMCCSession_t *session;
    pthread_mutex_lock(&sessionMutex);            
    ListIterator_ForEach(iterator, sessions)
    {
        session = ListIterator_Current(iterator);
        if (session->filter == filter)
        {
            LogModule(LOG_DEBUG, DSMCC, "Re-enabling DSMCC session for service filter %s", ServiceFilterNameGet(filter));
            pthread_mutex_lock(&sessionMutex);
            EnableSession(session);
            pthread_mutex_unlock(&sessionMutex);
            break;
        }
    }
    pthread_mutex_unlock(&sessionMutex);            
}

static void HandleTuningMultiplexChanged(void *arg, Event_t event, void *payload)
{
    Multiplex_t *mux = arg;
    ListIterator_t iterator, pidIterator;
    pthread_mutex_lock(&sessionMutex);            
    ListIterator_ForEach(iterator, downloadSessions)
    {
        DSMCCDownloadSession_t *session = ListIterator_Current(iterator);
        TSFilterGroupRemoveAllFilters(session->filterGroup);

        ListIterator_ForEach(pidIterator, session->pids)
        {
            DSMCCPID_t *pid = ListIterator_Current(pidIterator);
            if (session->service->multiplexUID == mux->uid)
            {
                pid->sectionFilter = dvbpsi_AttachSections(DSMCCSectionCallback, pid);
                TSFilterGroupAddSectionFilter(session->filterGroup, pid->pid, DSMCC_FILTER_PRIORITY, pid->sectionFilter);

            }
            else
            {
                if (pid->sectionFilter)
                {
                    
                    dvbpsi_DetachSections(pid->sectionFilter);
                    pid->sectionFilter = NULL;
                }
            }
        }
    }
    pthread_mutex_unlock(&sessionMutex);                
}

static void EnableSession(DSMCCSession_t *session)
{
    Service_t *service;
    if (session->downloadSession)
    {
        ObjectRefDec(session->downloadSession);
    }
    service = ServiceFilterServiceGet(session->filter);
    session->downloadSession = DownloadSessionGet(service);
}

static DSMCCDownloadSession_t *DownloadSessionGet(Service_t *service)
{
    ListIterator_t iterator;    
    DSMCCDownloadSession_t *session;
    
    ListIterator_ForEach(iterator, downloadSessions)
    {
        session = ListIterator_Current(iterator);
        if (ServiceAreEqual(service, session->service))
        {
            return session;
        }
    }

    session = ObjectCreateType(DSMCCDownloadSession_t);
    session->service = service;
    ServiceRefInc(service);
    ListAdd(downloadSessions, session);
    return session;
}

static void SessionDestructor(void *arg)
{
    DSMCCSession_t *session = arg;
    if (session->downloadSession)
    {
        ObjectRefDec(session->downloadSession);
    }
}

static void DownloadSessionDestructor(void *arg)
{
    DSMCCDownloadSession_t *session = arg;
    ServiceRefDec(session->service);
    ListRemove(downloadSessions, arg);
}

static void DSMCCPIDDestructor(void *arg)
{
    DSMCCPID_t *pid = arg;
    if (pid->sectionFilter)
    {
        dvbpsi_DetachSections(pid->sectionFilter);
    }
}

static uint16_t AssociationTagToPID(Service_t *service, uint16_t tag)
{
    int i;
    PIDList_t *pids = CachePIDsGet(service);
    if (pids == NULL)
    {
        return INVALID_PID;
    }
    for (i = 0; i < pids->count; i ++)
    {
        dvbpsi_descriptor_t *desc;
        for (desc = pids->pids[i].descriptors; desc; desc = desc->p_next)
        {
            if (desc->i_tag == TAG_ASSOCIATION_TAG_DESCRIPTOR)
            {
                dvbpsi_association_tag_dr_t *assoc_tag_dr = dvbpsi_DecodeAssociationTagDr(desc);
                if (assoc_tag_dr && (assoc_tag_dr->i_tag == tag))
                {
                    return pids->pids[i].pid;
                }
            }
        }
    }
    return INVALID_PID;
}

void DownloadSessionPIDAddTag(DSMCCDownloadSession_t *session, uint16_t tag)
{
    ListIterator_t iterator;
    Multiplex_t *mux;
    DSMCCPID_t *dsmccPID;
    uint16_t pid = AssociationTagToPID(session->service, tag);
    
    ListIterator_ForEach(iterator, session->pids)
    {
        dsmccPID = ListIterator_Current(iterator);
        if (dsmccPID->pid == pid)
        {
            /* Already filtering this PID */
            return;
        }
    }
    dsmccPID = ObjectCreateType(DSMCCPID_t);
    dsmccPID->pid = pid;
    ListAdd(session->pids, dsmccPID);
    
    mux = TuningCurrentMultiplexGet();
    if (mux->uid == session->service->multiplexUID)
    {
        dsmccPID->sectionFilter = dvbpsi_AttachSections(DSMCCSectionCallback, dsmccPID);
        TSFilterGroupAddSectionFilter(session->filterGroup, pid, DSMCC_FILTER_PRIORITY, dsmccPID->sectionFilter);
    }
    ObjectRefDec(mux);
}
/*
static void DownloadSessionPIDRemoveTag(DSMCCDownloadSession_t *session, uint16_t tag)
{
    ListIterator_t iterator;
    DSMCCPID_t *dsmccPID;
    uint16_t pid = AssociationTagToPID(session->service, tag);
    
    ListIterator_ForEach(iterator, session->pids)
    {
        dsmccPID = ListIterator_Current(iterator);
        if (dsmccPID->pid == pid)
        {
            ListRemoveCurrent(&iterator);
            ObjectRefDec(dsmccPID);
            TSFilterGroupRemoveSectionFilter(session->filterGroup, pid);
            break;
        }
    }
}

*/
static void DSMCCSectionCallback(void *p_cb_data, dvbpsi_handle h_dvbpsi, dvbpsi_psi_section_t* p_section)
{
    DSMCCPID_t *dsmccPID = p_cb_data;
    dsmcc_process_section(&dsmccPID->session->status, p_section->p_data, p_section->i_length, dsmccPID->pid);
    
}
