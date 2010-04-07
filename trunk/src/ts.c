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

ts.c

Transport stream processing and filter management.

*/
#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/types.h>

#include <dvbpsi/dvbpsi.h>
#include <dvbpsi/descriptor.h>
#include <dvbpsi/psi.h>
#include <dvbpsi/sections.h>

#include "multiplexes.h"
#include "services.h"
#include "ts.h"
#include "logging.h"
#include "dispatchers.h"

/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/
#define PACKET_FILTER_DISABLED 0x8000
#define TSREADER_PID_INVALID 0xffff

#define CHECK_PID_VALID(pid) \
    do{\
        if ((pid) > TSREADER_PID_ALL)\
        {\
            LogModule(LOG_ERROR, TSREADER, "Invalid PID %u supplied to %s", pid, __func__);\
            return;\
        }\
    }while(0)

/**
 * Minimum number of PID Filters to reserve for section filters.
 */
#define MIN_SECTION_FILTER_PIDS 4


/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void TSReaderStatsDestructor(void *ptr);
static void StatsAddFilterGroupStats(TSReaderStats_t *stats, const char *type, TSFilterGroupStats_t *filterGroupStats);
static void PromiscusModeEnable(TSReader_t *reader, bool enable);
static TSPacketFilter_t * PacketFilterListAddFilter(TSReader_t *reader, TSFilterGroup_t *group, uint16_t pid, TSPacketFilterCallback_t callback, void *userArg);
static void PacketFilterListRemoveFilter(TSReader_t *reader, TSPacketFilter_t *packetFilter);
static TSSectionFilterList_t * SectionFilterListCreate(TSReader_t *reader, uint16_t pid);
static void SectionFilterListDestroy(TSReader_t *reader, TSSectionFilterList_t *sfList);
static void SectionFilterListAddFilter(TSReader_t *reader, TSSectionFilter_t *filter);
static void SectionFilterListRemoveFilter(TSReader_t *reader, TSSectionFilter_t *filter);
static void SectionFilterListUpdatePriority(TSSectionFilterList_t *sfList);
static TSSectionFilterList_t * SectionFilterListFind(TSReader_t *reader, uint16_t pid);
static void SectionFilterListScheduleFilters(TSReader_t *reader);
static void SectionFilterListDescheduleFilters(TSReader_t *reader);
static void SectionFilterListDescheduleOneFilter(TSReader_t *reader);
static void SectionFilterListPacketCallback(void *userArg, struct TSFilterGroup_t *group, TSPacket_t *packet);
static void SectionFilterListPushSection(void *userArg, dvbpsi_handle sectionsHandle, dvbpsi_psi_section_t *section);

static void TSReaderDVRCallback(struct ev_loop *loop, ev_io *w, int revents);
static void TSReaderBitrateCallback(struct ev_loop *loop, ev_timer *w, int revents);
static void TSReaderNotificationCallback(struct ev_loop *loop, ev_async *w, int revents);

static void ProcessPacket(TSReader_t *reader, TSPacket_t *packet);
static void SendToPacketFilters(TSReader_t *reader, uint16_t pid, TSPacket_t *packet);
static void InformTSStructureChanged(TSReader_t *reader);
static void InformMultiplexChanged(TSReader_t *reader);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/

char PSISIPIDFilterType[] = "PSI/SI";
static char TSREADER[] = "TSReader";

/*******************************************************************************
* Transport Stream Filter Functions                                            *
*******************************************************************************/
TSReader_t* TSReaderCreate(DVBAdapter_t *adapter)
{
    TSReader_t *result;
    struct ev_loop *inputLoop;
    ObjectRegisterType(TSReader_t);
    ObjectRegisterType(TSFilterGroup_t);
    ObjectRegisterType(TSSectionFilter_t);
    ObjectRegisterType(TSSectionFilterList_t);
    ObjectRegisterType(TSPacketFilter_t);
    ObjectRegisterTypeDestructor(TSReaderStats_t, TSReaderStatsDestructor);
    ObjectRegisterType(TSFilterGroupTypeStats_t);
    ObjectRegisterType(TSFilterGroupStats_t);    

    result = ObjectCreateType(TSReader_t);
    if (result)
    {
        pthread_mutexattr_t mutexAttr;

        result->adapter = adapter;
        DVBDemuxSetBufferSize(adapter, TSREADER_MAX_PACKETS * TSPACKET_SIZE);
        result->groups = ListCreate();
        result->activeSectionFilters = ListCreate();
        result->sectionFilters = ListCreate();
        result->currentlyProcessingPid = TSREADER_PID_INVALID;
        pthread_mutexattr_init(&mutexAttr);
        pthread_mutexattr_settype(&mutexAttr, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&result->mutex, &mutexAttr);
        pthread_mutexattr_destroy(&mutexAttr);
        inputLoop = DispatchersGetInput();
        ev_io_init(&result->dvrWatcher, TSReaderDVRCallback, DVBDVRGetFD(adapter), EV_READ);
        ev_timer_init(&result->bitrateWatcher, TSReaderBitrateCallback, 1.0, 1.0);
        ev_async_init(&result->notificationWatcher, TSReaderNotificationCallback);
        result->dvrWatcher.data = result;
        result->bitrateWatcher.data = result;
        result->notificationWatcher.data = result;
        ev_io_start(inputLoop, &result->dvrWatcher);
        ev_timer_start(inputLoop, &result->bitrateWatcher);
        ev_async_start(inputLoop, &result->notificationWatcher);
    }
    return result;
}

void TSReaderDestroy(TSReader_t* reader)
{
    int i;
    struct ev_loop *inputLoop = DispatchersGetInput();
    ev_io_stop(inputLoop, &reader->dvrWatcher);
    ev_timer_stop(inputLoop, &reader->bitrateWatcher);
    SectionFilterListDescheduleFilters(reader);
    pthread_mutex_destroy(&reader->mutex);
    
    ListFree(reader->groups, (void (*)(void*))TSFilterGroupDestroy);
    
    for (i = 0; i < TSREADER_NROF_FILTERS; i ++)
    {
        if (reader->packetFilters[i] == NULL)
        {
            TSPacketFilter_t *cur, *next;
            DVBDemuxReleaseFilter(reader->adapter, i);
            for (cur = reader->packetFilters[i]; cur; cur = next)
            {
                next = cur->next;
                if (cur->group)
                {
                    ObjectRefDec(cur);
                }
            }
        }
    }
    ListFree(reader->activeSectionFilters,NULL);
    ListFree(reader->sectionFilters,NULL);

    ObjectRefDec(reader);
}


void TSReaderEnable(TSReader_t* reader, bool enable)
{
    pthread_mutex_lock(&reader->mutex);
    reader->enabled = enable;
    pthread_mutex_unlock(&reader->mutex);
}

TSReaderStats_t *TSReaderExtractStats(TSReader_t *reader)
{
    ListIterator_t iterator;
    TSReaderStats_t *stats = ObjectCreateType(TSReaderStats_t);
    
    pthread_mutex_lock(&reader->mutex);

    /* Clear all filter stats */
    stats->totalPackets = reader->totalPackets;
    stats->bitrate = reader->bitrate;

    for (ListIterator_Init(iterator, reader->groups); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        TSFilterGroup_t *group = (TSFilterGroup_t*)ListIterator_Current(iterator);
        TSFilterGroupStats_t * filterGroupStats = ObjectCreateType(TSFilterGroupStats_t);

        filterGroupStats->name = group->name;
        filterGroupStats->packetsProcessed = group->packetsProcessed;
        filterGroupStats->sectionsProcessed = group->sectionsProcessed;
        StatsAddFilterGroupStats(stats, group->type, filterGroupStats);
    }
    pthread_mutex_unlock(&reader->mutex);
    return stats;
}

void TSReaderZeroStats(TSReader_t* reader)
{
    ListIterator_t iterator;
    pthread_mutex_lock(&reader->mutex);
    /* Clear all filter stats */
    reader->totalPackets = 0;
    reader->bitrate = 0;

    for (ListIterator_Init(iterator, reader->groups); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        TSFilterGroup_t *group = (TSFilterGroup_t*)ListIterator_Current(iterator);
        group->packetsProcessed = 0;
        group->sectionsProcessed = 0;
    }
    pthread_mutex_unlock(&reader->mutex);
}

void TSReaderMultiplexChanged(TSReader_t *reader, Multiplex_t *newmultiplex)
{
    struct ev_loop *inputLoop = DispatchersGetInput();
    reader->multiplexChanged = TRUE;
    reader->multiplex = newmultiplex;
    LogModule(LOG_INFO, TSREADER, "Notifying mux changed!");
    ev_async_send(inputLoop, &reader->notificationWatcher);
}


void TSReaderSectionFilterOverridePriority(TSReader_t *reader, uint16_t pid, int priority)
{
    TSSectionFilterList_t *sfList;
    pthread_mutex_lock(&reader->mutex);    
    sfList = SectionFilterListFind(reader, pid);
    if (sfList)
    {
        sfList->priority = priority;
        sfList->flags |= TSSectFilterListFlags_PRIORITY_OVERRIDE;
    }
    pthread_mutex_unlock(&reader->mutex);
}

void TSReaderSectionFilterResetPriority(TSReader_t *reader, uint16_t pid)
{
    TSSectionFilterList_t *sfList;
    pthread_mutex_lock(&reader->mutex);
    sfList = SectionFilterListFind(reader, pid);
    if (sfList)
    {
        SectionFilterListUpdatePriority(sfList);
        sfList->flags &= ~TSSectFilterListFlags_PRIORITY_OVERRIDE;
    }
    pthread_mutex_unlock(&reader->mutex);
}

TSFilterGroup_t* TSReaderCreateFilterGroup(TSReader_t *reader, const char *name, const char *type, TSFilterGroupEventCallback_t callback, void *userArg )
{
    TSFilterGroup_t *group = ObjectCreateType(TSFilterGroup_t);
    if (group)
    {
        group->name = (char *)name;
        group->type = (char *)type;
        group->eventCallback = callback;
        group->userArg = userArg;
        group->tsReader = reader;
        pthread_mutex_lock(&reader->mutex);        
        ListAdd(reader->groups, group);
        pthread_mutex_unlock(&reader->mutex);
        
    }
    return group;
}

TSFilterGroup_t* TSReaderFindFilterGroup(TSReader_t *reader, const char *name, const char *type)
{    
    ListIterator_t iterator;
    TSFilterGroup_t* result = NULL;
    pthread_mutex_lock(&reader->mutex);
    for (ListIterator_Init(iterator, reader->groups); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        TSFilterGroup_t *group = ListIterator_Current(iterator);
        if ((strcmp(group->name, name) == 0) && (strcmp(group->type, type) == 0))
        {
            result = group;
            break;
        }
    }
    pthread_mutex_unlock(&reader->mutex);
    return result;
}


void TSFilterGroupDestroy(TSFilterGroup_t* group)
{
    LogModule(LOG_DEBUG, TSREADER, "Destroying filter group %s", group->name);
    TSFilterGroupRemoveAllFilters(group);
    pthread_mutex_lock(&group->tsReader->mutex);
    ListRemove(group->tsReader->groups, group);
    pthread_mutex_unlock(&group->tsReader->mutex);
    ObjectRefDec(group);
}

void  TSFilterGroupResetStats(TSFilterGroup_t* group)
{
    LogModule(LOG_DEBUG, TSREADER, "Resetting stats for filter group %s", group->name);
    pthread_mutex_lock(&group->tsReader->mutex);
    group->packetFilters = 0;
    group->sectionFilters = 0;
    pthread_mutex_unlock(&group->tsReader->mutex);
}

void TSFilterGroupRemoveAllFilters(TSFilterGroup_t* group)
{
    TSPacketFilter_t *packetFilter;
    TSPacketFilter_t *packetFilterNext;
    TSSectionFilter_t *sectionFilter;
    TSSectionFilter_t *sectionFilterNext;
    LogModule(LOG_DEBUG, TSREADER, "Removing all filters for filter group %s", group->name);    
    pthread_mutex_lock(&group->tsReader->mutex);    
    for (packetFilter = group->packetFilters; packetFilter; packetFilter = packetFilterNext)
    {
        LogModule(LOG_DEBUG, TSREADER, "Removing %p", packetFilter);
        packetFilterNext = packetFilter->next;
        PacketFilterListRemoveFilter(group->tsReader, packetFilter);
    }
    group->packetFilters = NULL;
    for (sectionFilter = group->sectionFilters; sectionFilter; sectionFilter = sectionFilterNext)
    {
        SectionFilterListRemoveFilter(group->tsReader, sectionFilter);
        sectionFilterNext = sectionFilter->next;
        ObjectRefDec(sectionFilter);
    }
    group->sectionFilters = NULL;
    pthread_mutex_unlock(&group->tsReader->mutex);    
}

void TSFilterGroupAddSectionFilter(TSFilterGroup_t *group, uint16_t pid, int priority, dvbpsi_handle handle)
{
    TSSectionFilter_t *sectionFilter;

    CHECK_PID_VALID(pid);
    
    sectionFilter = ObjectCreateType(TSSectionFilter_t);
    sectionFilter->pid = pid;
    sectionFilter->priority = priority;
    sectionFilter->sectionHandle = handle;
    sectionFilter->group = group;
    LogModule(LOG_DEBUG, TSREADER, "Adding section filter 0x%04x for filter group %s", pid, group->name);    
    pthread_mutex_lock(&group->tsReader->mutex); 
    sectionFilter->next = group->sectionFilters;
    group->sectionFilters = sectionFilter;
    SectionFilterListAddFilter(group->tsReader, sectionFilter);
    pthread_mutex_unlock(&group->tsReader->mutex);       
}

void TSFilterGroupRemoveSectionFilter(TSFilterGroup_t *group, uint16_t pid)
{
    TSSectionFilter_t *sectionFilter;
    TSSectionFilter_t *sectionFilterPrev = NULL;

    CHECK_PID_VALID(pid);
    
    LogModule(LOG_DEBUG, TSREADER, "Removing section filter 0x%04x for filter group %s", pid, group->name);        
    pthread_mutex_lock(&group->tsReader->mutex); 
    for (sectionFilter = group->sectionFilters; sectionFilter; sectionFilter = sectionFilter->next)
    {
        if (sectionFilter->pid == pid)
        {
            if (sectionFilterPrev)
            {
                sectionFilterPrev->next = sectionFilter->next;
            }
            else
            {
                group->sectionFilters = sectionFilter->next;
            }
            SectionFilterListRemoveFilter(group->tsReader, sectionFilter);
            ObjectRefDec(sectionFilter);
            break;
        }
        sectionFilterPrev = sectionFilter;
    }
    pthread_mutex_unlock(&group->tsReader->mutex);
}

bool TSFilterGroupAddPacketFilter(TSFilterGroup_t *group, uint16_t pid, TSPacketFilterCallback_t callback, void *userArg)
{
    TSPacketFilter_t *packetFilter;
    bool result = TRUE;

    if (pid > TSREADER_PID_ALL)
    {
        LogModule(LOG_INFO, TSREADER, "Invalid PID %u supplied to %s", pid, __func__);
        return FALSE;
    }
    

    pthread_mutex_lock(&group->tsReader->mutex);
    for (packetFilter = group->packetFilters; packetFilter; packetFilter = packetFilter->next)
    {
        if (packetFilter->pid == pid)
        {
            LogModule(LOG_DEBUG, TSREADER, "PID 0x%04x is already being packet filtered for filter group %s", pid, group->name);
            result = FALSE;
            break;
        }
    }
    if (result)
    {
        LogModule(LOG_DEBUG, TSREADER, "Adding packet filter 0x%04x for filter group %s", pid, group->name);
        packetFilter = PacketFilterListAddFilter(group->tsReader, group, pid, callback, userArg); 
        if (packetFilter != NULL)
        {
            packetFilter->next = group->packetFilters;
            group->packetFilters = packetFilter;
        }
        else
        {        
            result = FALSE;
        }
    }
    pthread_mutex_unlock(&group->tsReader->mutex);
    return result;
}

void TSFilterGroupRemovePacketFilter(TSFilterGroup_t *group, uint16_t pid)
{
    TSPacketFilter_t *packetFilter;
    TSPacketFilter_t *packetFilterPrev = NULL;

    CHECK_PID_VALID(pid);
    
    LogModule(LOG_DEBUG, TSREADER, "Removing packet filter 0x%04x for filter group %s", pid, group->name);        
    pthread_mutex_lock(&group->tsReader->mutex);
    for (packetFilter = group->packetFilters; packetFilter; packetFilter = packetFilter->next)
    {
        if (packetFilter->pid == pid)
        {
            if (packetFilterPrev)
            {
                packetFilterPrev->next = packetFilter->next;
            }
            else
            {
                group->packetFilters = packetFilter->next;
            }
            PacketFilterListRemoveFilter(group->tsReader, packetFilter);
            break;
        }
        packetFilterPrev = packetFilter;
    }
    pthread_mutex_unlock(&group->tsReader->mutex);
}


/*******************************************************************************
* Internal Functions                                                           *
*******************************************************************************/
static void TSReaderStatsDestructor(void *ptr)
{
    TSReaderStats_t *stats = ptr;
    TSFilterGroupTypeStats_t *typeStats;
    TSFilterGroupTypeStats_t *typeStatsNext;
    for (typeStats = stats->types; typeStats; typeStats = typeStatsNext)
    {
        TSFilterGroupStats_t *groupStats;
        TSFilterGroupStats_t *groupStatsNext;
        for (groupStats = typeStats->groups; groupStats; groupStats = groupStatsNext)
        {
            groupStatsNext = groupStats->next;
            ObjectRefDec(groupStats);
        }
        typeStatsNext = typeStats->next;
        ObjectRefDec(typeStats);
    }
}

static void StatsAddFilterGroupStats(TSReaderStats_t *stats, const char *type, TSFilterGroupStats_t *filterGroupStats)
{
    TSFilterGroupTypeStats_t *typeStats;
    for (typeStats = stats->types; typeStats; typeStats = typeStats->next)
    {
        if (strcmp(typeStats->type, type) == 0)
        {
            break;
        }
    }
    if (typeStats == NULL)
    {
        typeStats = ObjectCreateType(TSFilterGroupTypeStats_t);
        typeStats->type = (char*)type;
        typeStats->next = stats->types;
        stats->types = typeStats;
    }
    filterGroupStats->next = typeStats->groups;
    typeStats->groups = filterGroupStats;
}

static void PromiscusModeEnable(TSReader_t *reader, bool enable)
{
    int i;

    if (enable == reader->promiscuousMode)
    {
        return;
    }

    for (i = 0; i < TSREADER_NROF_FILTERS; i ++)
    {
        if (reader->packetFilters[i])
        {
            if (enable)
            {
                DVBDemuxReleaseFilter(reader->adapter, i);
            }
            else
            {
                DVBDemuxAllocateFilter(reader->adapter, i);
                LogModule(LOG_INFO, TSREADER, "Failed to allocate filter for 0x%04x", i);
            }
        }

    }
    if (enable)
    {
        DVBDemuxAllocateFilter(reader->adapter, TSREADER_PID_ALL);

    }
    else
    {
        DVBDemuxReleaseFilter(reader->adapter, TSREADER_PID_ALL);
    }
    reader->promiscuousMode = enable;
}

static TSPacketFilter_t * PacketFilterListAddFilter(TSReader_t *reader, TSFilterGroup_t *group, uint16_t pid, TSPacketFilterCallback_t callback, void *userArg)
{
    TSPacketFilter_t *packetFilter;
    if (reader->packetFilters[pid] == NULL)
    {
        if (!reader->promiscuousMode && (pid != TSREADER_PID_ALL))
        {
            int p, pidCount = 0;
            int freePIDCount;
            
            for (p = 0; p < TSREADER_NROF_FILTERS; p ++)
            {
                pidCount += (reader->packetFilters[p] != NULL) ?  1:0;
            }

            freePIDCount = DVBDemuxGetMaxFilters(reader->adapter) - pidCount;
            if (ListCount(reader->activeSectionFilters) < MIN_SECTION_FILTER_PIDS)
            {
                freePIDCount -= MIN_SECTION_FILTER_PIDS - ListCount(reader->activeSectionFilters);
            }
            
            if ((group != NULL) && (freePIDCount <= 0))
            {
                return NULL;
            }
            
            if (DVBDemuxAllocateFilter(reader->adapter, pid))
            {
                if (DVBDemuxIsHardwareRestricted(reader->adapter))
                {
                    /* If this is a section filter and we are HW restricted fail straight away*/
                    if (group == NULL)
                    {
                        return NULL;
                    }
                    /* If this is not for a section filter and we are HW restricted try 
                     * and release a Section Filter PID filter so we can start this one.
                     */
                    if ((group != NULL) && (ListCount(reader->activeSectionFilters) > MIN_SECTION_FILTER_PIDS))
                    {
                        SectionFilterListDescheduleOneFilter(reader);
                    }
                    if (DVBDemuxAllocateFilter(reader->adapter, pid))
                    {
                        LogModule(LOG_INFO, TSREADER, "Failed to allocate filter for 0x%04x", pid);
                        return NULL;
                    }
                }
                else
                {
                    PromiscusModeEnable(reader, TRUE);
                }

            }
        }

        if (!DVBDemuxIsHardwareRestricted(reader->adapter) && !reader->promiscuousMode && (pid == TSREADER_PID_ALL))
        {
            PromiscusModeEnable(reader, TRUE);
        }
    }
    packetFilter = ObjectCreateType(TSPacketFilter_t);
    packetFilter->pid = pid;
    packetFilter->group = group;
    packetFilter->callback = callback;
    packetFilter->userArg = userArg;
    packetFilter->flNext = reader->packetFilters[pid];
    reader->packetFilters[pid] = packetFilter;
    return packetFilter;
}

static void PacketFilterListRemoveFilter(TSReader_t *reader, TSPacketFilter_t *packetFilter)
{
    TSPacketFilter_t *cur = NULL;
    TSPacketFilter_t *prev = NULL;

    LogModule(LOG_DEBUG, TSREADER, "Removing packet filter %p on pid 0x%02x", packetFilter, packetFilter->pid);
    if (reader->currentlyProcessingPid == (packetFilter->pid & ~PACKET_FILTER_DISABLED))
    {
        LogModule(LOG_DEBUG, TSREADER, "Removing packet filter (replaced with NULL)");
        packetFilter->pid |= PACKET_FILTER_DISABLED;
        return;
    }
    for (cur = reader->packetFilters[packetFilter->pid]; cur; cur = cur->flNext)
    {
        if (cur == packetFilter)
        {
            break;
        }
        prev = cur;
    }
    if (cur == packetFilter)
    {
        if (prev == NULL)
        {
            reader->packetFilters[packetFilter->pid] = cur->flNext;
        }
        else
        {
            prev->flNext = cur->flNext;
        }
    }
    if (reader->packetFilters[packetFilter->pid] == NULL)
    {
        if (!reader->promiscuousMode && (packetFilter->pid != TSREADER_PID_ALL))
        {
            DVBDemuxReleaseFilter(reader->adapter, packetFilter->pid);
        }
    }
    ObjectRefDec(packetFilter);
}

static TSSectionFilterList_t * SectionFilterListCreate(TSReader_t *reader, uint16_t pid)
{
    TSSectionFilterList_t *sfList = ObjectCreateType(TSSectionFilterList_t);
    sfList->pid = pid;
    sfList->filters = ListCreate();
    sfList->tsReader = reader;
    sfList->sectionHandle = dvbpsi_AttachSections(SectionFilterListPushSection, sfList);
    ListAdd(reader->sectionFilters, sfList);
    return sfList;
}

static void SectionFilterListDestroy(TSReader_t *reader, TSSectionFilterList_t *sfList)
{
    if (ListRemove(reader->activeSectionFilters, sfList))
    {
        LogModule(LOG_DEBUG, TSREADER, "Removed active section filter %p", sfList);
        PacketFilterListRemoveFilter(reader, sfList->packetFilter);
    }
    else
    {
        LogModule(LOG_DEBUG, TSREADER, "Removed section filter %p", sfList);
        ListRemove(reader->sectionFilters, sfList);
    }

    ListFree(sfList->filters, NULL);
    dvbpsi_DetachSections(sfList->sectionHandle);
    ObjectRefDec(sfList);
}

static void SectionFilterListAddFilter(TSReader_t *reader, TSSectionFilter_t *filter)
{
    TSSectionFilterList_t *sfList = SectionFilterListFind(reader, filter->pid);
    if (sfList == NULL)
    {
        sfList = SectionFilterListCreate(reader, filter->pid);
        if  (sfList == NULL)
        {
            return;
        }
    }
    ListAdd(sfList->filters, filter);
    SectionFilterListUpdatePriority(sfList);
    SectionFilterListScheduleFilters(reader);
}

static void SectionFilterListRemoveFilter(TSReader_t *reader, TSSectionFilter_t *filter)
{
    TSSectionFilterList_t *sfList = SectionFilterListFind(reader, filter->pid);
    if (sfList)
    {
        ListRemove(sfList->filters, filter);
        if (ListCount(sfList->filters) == 0)
        {
            SectionFilterListDestroy(reader, sfList);
        }
        else
        {
            SectionFilterListUpdatePriority(sfList);
        }
    }
}

static void SectionFilterListUpdatePriority(TSSectionFilterList_t *sfList)
{
    ListIterator_t iterator;
    bool first = TRUE;
    int currentPriority = 0;
    
    if (sfList->flags & TSSectFilterListFlags_PRIORITY_OVERRIDE)
    {
        return;
    }

    for (ListIterator_Init(iterator, sfList->filters); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        TSSectionFilter_t *filter = ListIterator_Current(iterator);
        if (first)
        {
            currentPriority = filter->priority;
            first = FALSE;
        }
        else
        {
            if (currentPriority > filter->priority)
            {
                currentPriority = filter->priority;
            }
        }
    }
    sfList->priority = currentPriority;
}

static TSSectionFilterList_t * SectionFilterListFind(TSReader_t *reader, uint16_t pid)
{
    ListIterator_t iterator;
    for (ListIterator_Init(iterator, reader->activeSectionFilters); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        TSSectionFilterList_t *sfList = ListIterator_Current(iterator);
        if (sfList->pid == pid)
        {
            return sfList;
        }
    }
    for (ListIterator_Init(iterator, reader->sectionFilters); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        TSSectionFilterList_t *sfList = ListIterator_Current(iterator);
        if (sfList->pid == pid)
        {
            return sfList;
        }
    }

    return NULL;
}

static void SectionFilterListScheduleFilters(TSReader_t *reader)
{
    ListIterator_t iterator;
    LogModule(LOG_DEBUG, TSREADER, "Scheduling section filters");
    /* TODO: Take into account priority */
    for (ListIterator_Init(iterator, reader->sectionFilters); ListIterator_MoreEntries(iterator);)
    {
        TSSectionFilterList_t *sfList = ListIterator_Current(iterator);
        sfList->flags |= TSSectFilterListFlags_PAYLOAD_START;
        sfList->packetFilter = PacketFilterListAddFilter(reader, NULL, sfList->pid, SectionFilterListPacketCallback, sfList);
        if (sfList->packetFilter == NULL)
        {
            break;
        }
        ListRemoveCurrent(&iterator);
        ListAdd(reader->activeSectionFilters, sfList);
    }
}

static void SectionFilterListDescheduleFilters(TSReader_t *reader)
{
    ListIterator_t iterator;
    LogModule(LOG_DEBUG, TSREADER, "Descheduling section filters");
    for (ListIterator_Init(iterator, reader->activeSectionFilters); ListIterator_MoreEntries(iterator);)
    {
        TSSectionFilterList_t *sfList = ListIterator_Current(iterator);
        PacketFilterListRemoveFilter(sfList->tsReader, sfList->packetFilter);
        sfList->packetFilter = NULL;
        ListRemoveCurrent(&iterator);
        ListAdd(reader->sectionFilters, sfList);
    }
}

static void SectionFilterListDescheduleOneFilter(TSReader_t *reader)
{
    ListIterator_t iterator;
    TSSectionFilterList_t *toDeschedule = NULL;
    
    LogModule(LOG_DEBUG, TSREADER, "Descheduling one section filter");
    for (ListIterator_Init(iterator, reader->activeSectionFilters); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        TSSectionFilterList_t *sfList = ListIterator_Current(iterator);

        if ((reader->packetFilters[sfList->pid] == sfList->packetFilter) && (sfList->packetFilter->flNext == NULL))
        {
            if ((toDeschedule == NULL) || (toDeschedule->priority < sfList->priority))
            {
                toDeschedule = sfList;
            }
        }
    }
    if (toDeschedule)
    {
        LogModule(LOG_DEBUG, TSREADER, "Chose %d to deschedule.", toDeschedule->pid);
        ListRemove(reader->activeSectionFilters, toDeschedule);
        ListAdd(reader->sectionFilters, toDeschedule);
        PacketFilterListRemoveFilter(reader, toDeschedule->packetFilter);
        toDeschedule->packetFilter = NULL;
    }
}


static void SectionFilterListPacketCallback(void *userArg, struct TSFilterGroup_t *group, TSPacket_t *packet)
{
    TSSectionFilterList_t *sfList = userArg;
    if (sfList->flags & TSSectFilterListFlags_PAYLOAD_START)
    {
        if (!TSPACKET_ISPAYLOADUNITSTART(*packet))
        {
            return;
        }
        sfList->flags &= ~TSSectFilterListFlags_PAYLOAD_START;
    }
    dvbpsi_PushPacket(sfList->sectionHandle, (uint8_t *) packet);
}

static void SectionFilterListPushSection(void *userArg, dvbpsi_handle sectionsHandle, dvbpsi_psi_section_t *section)
{
    TSSectionFilterList_t *sfList = userArg;
    ListIterator_t iterator;
    dvbpsi_psi_section_t *cloned;
    
    for (ListIterator_Init(iterator, sfList->filters); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        TSSectionFilter_t *filter = ListIterator_Current(iterator);
        cloned = dvbpsi_ClonePSISection(filter->sectionHandle, section);
        if (filter->group)
        {
            filter->group->sectionsProcessed ++;
        }
        dvbpsi_PushSection(filter->sectionHandle, cloned);
    }
    
    dvbpsi_ReleasePSISections(sfList->sectionHandle, section);

    if (sfList->packetFilter && ListCount(sfList->tsReader->sectionFilters))
    {
        ListRemove(sfList->tsReader->activeSectionFilters, sfList);
        ListAdd(sfList->tsReader->sectionFilters, sfList);
        PacketFilterListRemoveFilter(sfList->tsReader, sfList->packetFilter);
        sfList->packetFilter = NULL;
    }
}

static void TSReaderDVRCallback(struct ev_loop *loop, ev_io *w, int revents)
{
    TSReader_t *reader = (TSReader_t*)w->data;
    DVBAdapter_t *adapter = reader->adapter;
    int count, p;
  
    count = read(DVBDVRGetFD(adapter), (char*)reader->buffer, sizeof(reader->buffer));
    if (!DVBFrontEndIsLocked(adapter) || !reader->enabled)
    {
        return;
    }
    pthread_mutex_lock(&reader->mutex);
    for (p = 0; (p < (count / TSPACKET_SIZE)) && reader->enabled; p ++)
    {
        if (!TSPACKET_ISVALID(reader->buffer[p]))
        {
            continue;
        }
        ProcessPacket(reader, &reader->buffer[p]);
        
        /* The structure of the transport stream has changed in a major way,
            (ie new services, services removed) so inform all of the filters
            that are interested.
          */
        if (reader->tsStructureChanged)
        {
            InformTSStructureChanged(reader);
            reader->tsStructureChanged = FALSE;
        }
    }
    pthread_mutex_unlock(&reader->mutex);
}

static void TSReaderBitrateCallback(struct ev_loop *loop, ev_timer *w, int revents)
{
    TSReader_t *reader = (TSReader_t*)w->data;
    reader->bitrate = (unsigned long)((reader->totalPackets - reader->prevTotalPackets) * (188 * 8));
    reader->prevTotalPackets = reader->totalPackets;
}

static void TSReaderNotificationCallback(struct ev_loop *loop, ev_async *w, int revents)
{
    TSReader_t *reader = (TSReader_t*)w->data;

    if (reader->multiplexChanged)
    {
        LogModule(LOG_INFO, TSREADER, "Informing mux changed!");
        InformMultiplexChanged(reader);
        reader->multiplexChanged = FALSE;
    }
}
    

static void ProcessPacket(TSReader_t *reader, TSPacket_t *packet)
{
    SendToPacketFilters(reader, TSPACKET_GETPID(*packet), packet);
    SendToPacketFilters(reader, TSREADER_PID_ALL, packet);        
    reader->totalPackets ++;
}

static void SendToPacketFilters(TSReader_t *reader, uint16_t pid, TSPacket_t *packet)
{
    TSPacketFilter_t *cur, *prev=NULL, *next;
    if (reader->packetFilters[pid] == NULL)
    {
        return;
    }
    reader->currentlyProcessingPid = pid;
    for (cur = reader->packetFilters[pid]; cur; cur = cur->flNext)
    {
        if (cur->pid & PACKET_FILTER_DISABLED)
        {
            continue;
        }
        if (cur->group)
        {
            cur->group->packetsProcessed ++;
        }
        cur->callback(cur->userArg, cur->group, packet);
    }
    reader->currentlyProcessingPid = TSREADER_PID_INVALID;
    for (cur = reader->packetFilters[pid]; cur; cur = next)
    {
        next = cur->flNext;
        if (cur->pid & PACKET_FILTER_DISABLED)
        {
            LogModule(LOG_DEBUG, TSREADER, "Removing %p (%d) as marked as disabled", cur, cur->pid & 0x7fff);
            if (prev == NULL)
            {
                reader->packetFilters[pid] = cur->flNext;
            }
            else
            {
                prev->flNext = cur->flNext;
            }
            ObjectRefDec(cur);
        }
        else
        {
            prev = cur;
        }
    }
    if (reader->packetFilters[pid] == NULL)
    {
        if (!reader->promiscuousMode && (pid != TSREADER_PID_ALL))
        {
            DVBDemuxReleaseFilter(reader->adapter, pid);
        }
        SectionFilterListScheduleFilters(reader);
    }
}

static void InformTSStructureChanged(TSReader_t *reader)
{
    ListIterator_t iterator;
    for (ListIterator_Init(iterator, reader->groups); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        TSFilterGroup_t *group =(TSFilterGroup_t *)ListIterator_Current(iterator);
        if (group->eventCallback)
        {
            group->eventCallback(group->userArg, group, TSFilterEventType_StructureChanged, NULL);
        }
    }
}

static void InformMultiplexChanged(TSReader_t *reader)
{
    ListIterator_t iterator;
    for (ListIterator_Init(iterator, reader->groups); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        TSFilterGroup_t *group =(TSFilterGroup_t *)ListIterator_Current(iterator);
        if (group->eventCallback)
        {
            group->eventCallback(group->userArg, group, TSFilterEventType_MuxChanged, reader->multiplex);
        }
    }
}
