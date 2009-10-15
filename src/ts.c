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
#include <poll.h>

#include <dvbpsi/dvbpsi.h>
#include <dvbpsi/descriptor.h>
#include <dvbpsi/psi.h>
#include <dvbpsi/sections.h>
#include "multiplexes.h"
#include "services.h"
#include "ts.h"
#include "logging.h"

/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/
#define PACKET_FILTER_DISABLED 0x8000
#define TSREADER_PID_INVALID 0xffff

/**
 * Maximum number of packets to read from the DVB adapter in one go,
 */
#define MAX_PACKETS 20

/**
 * Minimum number of PID Filters to reserve for section filters.
 */
#define MIN_SECTION_FILTER_PIDS 4

/**
 * Retrieve the bucket a specific pid filter should be stored in.
 */
#define TSREADER_PIDFILTER_GETBUCKET(_reader, _pid) (_reader)->pidFilterBuckets[(_pid) / (TS_MAX_PIDS / TSREADER_PIDFILTER_BUCKETS)]

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void NotifyThread(TSReader_t* reader, char id);
static void TSReaderStatsDestructor(void *ptr);
static void StatsAddFilterGroupStats(TSReaderStats_t *stats, const char *type, TSFilterGroupStats_t *filterGroupStats);
static void PromiscusModeEnable(TSReader_t *reader, bool enable);
static bool PacketFilterListAddFilter(TSReader_t *reader, TSPacketFilter_t *packetFilter);
static bool PacketFilterListRemoveFilter(TSReader_t *reader, TSPacketFilter_t *packetFilter);
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

static void *FilterTS(void *arg);
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

        if (pipe(result->notificationFds) == -1)
        {
            LogModule(LOG_ERROR, TSREADER, "Failed to create notification file descriptors!");
            ObjectRefDec(result);
            return NULL;
        }
        result->groups = ListCreate();
        result->activeSectionFilters = ListCreate();
        result->sectionFilters = ListCreate();
        result->currentlyProcessingPid = TSREADER_PID_INVALID;
        pthread_mutexattr_init(&mutexAttr);
        pthread_mutexattr_settype(&mutexAttr, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&result->mutex, &mutexAttr);
        pthread_mutexattr_destroy(&mutexAttr);
        
        pthread_create(&result->thread, NULL, FilterTS, result);
    }
    return result;
}

void TSReaderDestroy(TSReader_t* reader)
{
    int i;
    SectionFilterListDescheduleFilters(reader);
    reader->quit = TRUE;
    NotifyThread(reader, 'q');
    pthread_join(reader->thread, NULL);
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
    pthread_mutex_lock(&reader->mutex);
    reader->multiplexChanged = TRUE;
    reader->multiplex = newmultiplex;
    NotifyThread(reader, 'm');
    pthread_mutex_unlock(&reader->mutex);
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
        if (PacketFilterListRemoveFilter(group->tsReader, packetFilter))
        {
            ObjectRefDec(packetFilter);
        }
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
    TSSectionFilter_t *sectionFilter = ObjectCreateType(TSSectionFilter_t);
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
    }
    pthread_mutex_unlock(&group->tsReader->mutex);
}

bool TSFilterGroupAddPacketFilter(TSFilterGroup_t *group, uint16_t pid, TSPacketFilterCallback_t callback, void *userArg)
{
    TSPacketFilter_t *packetFilter = ObjectCreateType(TSPacketFilter_t);
    bool result = TRUE;

    LogModule(LOG_DEBUG, TSREADER, "Adding packet filter 0x%04x for filter group %s", pid, group->name);        
    packetFilter->pid = pid;
    packetFilter->userArg = userArg;
    packetFilter->callback = callback;
    packetFilter->group = group;
    pthread_mutex_lock(&group->tsReader->mutex);
    if (PacketFilterListAddFilter(group->tsReader, packetFilter))
    {
        packetFilter->next = group->packetFilters;
        group->packetFilters = packetFilter;
    }
    else
    {        
        ObjectRefDec(packetFilter);
        result = FALSE;
    }
    pthread_mutex_unlock(&group->tsReader->mutex);
    return result;
}

void TSFilterGroupRemovePacketFilter(TSFilterGroup_t *group, uint16_t pid)
{
    TSPacketFilter_t *packetFilter;
    TSPacketFilter_t *packetFilterPrev = NULL;

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
            if (PacketFilterListRemoveFilter(group->tsReader, packetFilter))
            {
                ObjectRefDec(packetFilter);
            }
            break;
        }
        packetFilterPrev = packetFilter;
    }
    pthread_mutex_unlock(&group->tsReader->mutex);
}


/*******************************************************************************
* Internal Functions                                                           *
*******************************************************************************/
static void NotifyThread(TSReader_t* reader, char id)
{
    LogModule(LOG_DEBUG, TSREADER, "Sending notification %c", id);
    pthread_mutex_lock(&reader->mutex);
    if (!reader->notificationSent)
    {
        reader->notificationSent = TRUE;
        if (write(reader->notificationFds[1], &id, 1) == -1)
        {
            LogModule(LOG_ERROR, TSREADER, "Failed to send notification!");
        }
    }
    pthread_mutex_unlock(&reader->mutex);
    LogModule(LOG_DEBUG, TSREADER, "Notification %c sent", id);
}

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

static bool PacketFilterListAddFilter(TSReader_t *reader, TSPacketFilter_t *packetFilter)
{
    if (reader->packetFilters[packetFilter->pid] == NULL)
    {
        if (!reader->promiscuousMode && (packetFilter->pid != TSREADER_PID_ALL))
        {
            int p, pidCount = 0;
            int freePIDCount;
            
            for (p = 0; p < TSREADER_NROF_FILTERS; p ++)
            {
                pidCount += (reader->packetFilters[p] != NULL) ?  1:0;
            }

            freePIDCount = reader->adapter->maxFilters - pidCount;
            if (ListCount(reader->activeSectionFilters) < MIN_SECTION_FILTER_PIDS)
            {
                freePIDCount -= MIN_SECTION_FILTER_PIDS - ListCount(reader->activeSectionFilters);
            }
            
            if ((packetFilter->group != NULL) && (freePIDCount <= 0))
            {
                return FALSE;
            }
            
            if (DVBDemuxAllocateFilter(reader->adapter, packetFilter->pid))
            {
                if (reader->adapter->hardwareRestricted)
                {
                    /* If this is a section filter and we are HW restricted fail straight away*/
                    if (packetFilter->group == NULL)
                    {
                        return FALSE;
                    }
                    /* If this is not for a section filter and we are HW restricted try 
                     * and release a Section Filter PID filter so we can start this one.
                     */
                    if ((packetFilter->group != NULL) && (ListCount(reader->activeSectionFilters) > MIN_SECTION_FILTER_PIDS))
                    {
                        SectionFilterListDescheduleOneFilter(reader);
                    }
                    if (DVBDemuxAllocateFilter(reader->adapter, packetFilter->pid))
                    {
                        LogModule(LOG_INFO, TSREADER, "Failed to allocate filter for 0x%04x", packetFilter->pid);
                        return FALSE;
                    }
                }
                else
                {
                    PromiscusModeEnable(reader, TRUE);
                }

            }
        }

        if (!reader->adapter->hardwareRestricted && !reader->promiscuousMode && (packetFilter->pid == TSREADER_PID_ALL))
        {
            PromiscusModeEnable(reader, TRUE);
        }
    }
    packetFilter->flNext = reader->packetFilters[packetFilter->pid];
    reader->packetFilters[packetFilter->pid] = packetFilter;
    return TRUE;
}

static bool PacketFilterListRemoveFilter(TSReader_t *reader, TSPacketFilter_t *packetFilter)
{
    LogModule(LOG_DEBUG, TSREADER, "Removing packet filter %p on pid 0x%02x", packetFilter, packetFilter->pid);
    if (reader->currentlyProcessingPid == (packetFilter->pid & ~PACKET_FILTER_DISABLED))
    {
        LogModule(LOG_DEBUG, TSREADER, "Removing packet filter (replaced with NULL)");
        packetFilter->pid |= PACKET_FILTER_DISABLED;
        return FALSE;
    }
    else
    {
        TSPacketFilter_t *cur = NULL;
        TSPacketFilter_t *prev = NULL;
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
    }
    return TRUE;
}

static TSSectionFilterList_t * SectionFilterListCreate(TSReader_t *reader, uint16_t pid)
{
    TSSectionFilterList_t *sfList = ObjectCreateType(TSSectionFilterList_t);
    sfList->pid = pid;
    sfList->filters = ListCreate();
    sfList->tsReader = reader;
    sfList->sectionHandle = dvbpsi_AttachSections(SectionFilterListPushSection, sfList);
    sfList->packetFilter.pid = sfList->pid;
    sfList->packetFilter.userArg = sfList;
    sfList->packetFilter.callback = SectionFilterListPacketCallback;
    ListAdd(reader->sectionFilters, sfList);
    return sfList;
}

static void SectionFilterListDestroy(TSReader_t *reader, TSSectionFilterList_t *sfList)
{
    if (ListRemove(reader->activeSectionFilters, sfList))
    {
        LogModule(LOG_DEBUG, TSREADER, "Removed active section filter %p", sfList);
        PacketFilterListRemoveFilter(reader, &sfList->packetFilter);
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
        sfList->packetFilter.pid = sfList->pid;
        sfList->packetFilter.next = NULL;
        sfList->packetFilter.flNext = NULL;
        if (!PacketFilterListAddFilter(reader, &sfList->packetFilter))
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
        PacketFilterListRemoveFilter(sfList->tsReader, &sfList->packetFilter);
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

        if ((reader->packetFilters[sfList->pid] == &sfList->packetFilter) && (sfList->packetFilter.flNext == NULL))
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
        PacketFilterListRemoveFilter(reader, &toDeschedule->packetFilter);
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

    if (ListCount(sfList->tsReader->sectionFilters))
    {
        ListRemove(sfList->tsReader->activeSectionFilters, sfList);
        ListAdd(sfList->tsReader->sectionFilters, sfList);
        PacketFilterListRemoveFilter(sfList->tsReader, &sfList->packetFilter);
    }
}

#define MAX_FDS 2
static void *FilterTS(void *arg)
{
    struct timeval now, last;
    int diff;
    unsigned long long prevpackets = 0;
    struct pollfd pfd[MAX_FDS];
    TSReader_t *reader = (TSReader_t*)arg;
    DVBAdapter_t *adapter = reader->adapter;
    int count = 0;
    TSPacket_t *readBuffer = NULL;
    int readBufferSize;
    
    LogRegisterThread(pthread_self(), TSREADER);

    readBufferSize = sizeof(TSPacket_t) * MAX_PACKETS;
    readBuffer = ObjectAlloc(readBufferSize);
    if (readBuffer == NULL)
    {
        LogModule(LOG_ERROR, TSREADER, "Failed to allocate packet read buffer!");
        return NULL;
    }
    DVBDemuxSetBufferSize(adapter, readBufferSize * 2);
    

    gettimeofday(&last, 0);
      
   
    
    while (!reader->quit)
    {
        int p;
        int n;

        pfd[0].fd = reader->notificationFds[0];
        pfd[0].events = POLLIN;
        pfd[0].revents = 0;
        pfd[1].fd = DVBDVRGetFD(adapter);
        pfd[1].events = POLLIN;
        pfd[1].revents = 0;

        n = poll(pfd, MAX_FDS, -1);
        if (n == -1)
        {
            LogModule(LOG_ERROR, TSREADER, "Poll() failed! %s");
            ObjectFree(readBuffer);
            return NULL;
        }

        if (pfd[0].revents & POLLIN)
        {
            int i;
            char id;
            pthread_mutex_lock(&reader->mutex);
            reader->notificationSent = FALSE;
            pthread_mutex_unlock(&reader->mutex);
            i = read(reader->notificationFds[0], &id, 1);
            
            if (reader->quit)
            {
                break;
            }

            if (reader->multiplexChanged)
            {
                InformMultiplexChanged(reader);
                reader->multiplexChanged = FALSE;
            }            
        }
        
        if (pfd[1].revents & POLLIN)
        {
            /* Read in packet */
            count = DVBDVRRead(adapter, (char*)readBuffer, readBufferSize, 1000);
            if (!adapter->frontEndLocked || !reader->enabled)
            {
                continue;
            }
            pthread_mutex_lock(&reader->mutex);
            for (p = 0; (p < (count / TSPACKET_SIZE)) && reader->enabled; p ++)
            {
                if (!TSPACKET_ISVALID(readBuffer[p]))
                {
                    continue;
                }
                ProcessPacket(reader, &readBuffer[p]);
                
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
            gettimeofday(&now, 0);
            diff =(now.tv_sec - last.tv_sec) * 1000 + (now.tv_usec - last.tv_usec) / 1000;
            if (diff > 1000)
            {
                // Work out bit rates
                reader->bitrate = (unsigned long)((reader->totalPackets - prevpackets) * (188 * 8));
                prevpackets = reader->totalPackets;
                last = now;
            }
            pthread_mutex_unlock(&reader->mutex);
        }
        
    }
    ObjectFree(readBuffer);    
    LogModule(LOG_DEBUG, TSREADER, "Filter thread exiting.\n");
    return NULL;
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
            /* Only free the filter if this is not a section filter */
            if (cur->group)
            {
                ObjectRefDec(cur);
            }
            else
            {
                cur->pid &= ~PACKET_FILTER_DISABLED;
            }
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
