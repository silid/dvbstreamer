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
static TSPacketFilterList_t *PacketFilterListCreate(TSReader_t *reader, uint16_t pid, bool forSF);
static void PacketFilterListDestroy(TSReader_t *reader, TSPacketFilterList_t *pfList);
static TSPacketFilterList_t *PacketFilterListFind(TSReader_t *reader, uint16_t pid);
static bool PacketFilterListAddFilter(TSReader_t *reader, TSPacketFilter_t *packetFilter);
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

static void *FilterTS(void *arg);
static void ProcessPacket(TSReader_t *state, TSPacket_t *packet);
static void SendToPacketFilters(TSPacketFilterList_t *pfList, TSPacket_t *packet);
static void InformTSStructureChanged(TSReader_t *state);
static void InformMultiplexChanged(TSReader_t *state);

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
    ObjectRegisterType(TSPacketFilterList_t);
    ObjectRegisterTypeDestructor(TSReaderStats_t, TSReaderStatsDestructor);
    ObjectRegisterType(TSFilterGroupTypeStats_t);
    ObjectRegisterType(TSFilterGroupStats_t);    

    result = ObjectCreateType(TSReader_t);
    if (result)
    {
        int i;
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

        for (i = 0; i < TSREADER_PIDFILTER_BUCKETS; i ++)
        {
            result->pidFilterBuckets[i] = ListCreate();
        }
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
    
    for (i = 0; i < TSREADER_PIDFILTER_BUCKETS; i ++)
    {
        ListFree(reader->pidFilterBuckets[i], NULL);
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
        PacketFilterListRemoveFilter(group->tsReader, packetFilter);
        packetFilterNext = packetFilter->next;
        LogModule(LOG_DEBUG, TSREADER, "Removed %p next %p", packetFilter, packetFilterNext);
        ObjectRefDec(packetFilter);
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
            PacketFilterListRemoveFilter(group->tsReader, packetFilter);
            ObjectRefDec(packetFilter);
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
    ListIterator_t iterator;
    if (enable == reader->promiscuousMode)
    {
        return;
    }

    for (i = 0; i < TSREADER_PIDFILTER_BUCKETS; i ++)
    {
        ListIterator_ForEach(iterator, reader->pidFilterBuckets[i])
        {
            TSPacketFilterList_t *pfList = ListIterator_Current(iterator);
            if (enable)
            {
                DVBDemuxReleaseFilter(reader->adapter, pfList->pid);
            }
            else
            {
                DVBDemuxAllocateFilter(reader->adapter, pfList->pid);
                LogModule(LOG_INFO, TSREADER, "Failed to allocate filter for 0x%04x", pfList->pid);
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

static TSPacketFilterList_t *PacketFilterListCreate(TSReader_t *reader, uint16_t pid, bool forSF)
{
    List_t *list;
    LogModule(LOG_DEBUG, TSREADER, "Creating packet filter 0x%04x", pid);    
    if (!reader->promiscuousMode && (pid != TSREADER_PID_ALL))
    {
        int p, pidCount = 0;
        int freePIDCount;
        
        for (p = 0; p < TSREADER_PIDFILTER_BUCKETS; p ++)
        {
            pidCount += ListCount(reader->pidFilterBuckets[p]);
        }

        freePIDCount = reader->adapter->maxFilters - pidCount;
        if (ListCount(reader->activeSectionFilters) < MIN_SECTION_FILTER_PIDS)
        {
            freePIDCount -= MIN_SECTION_FILTER_PIDS - ListCount(reader->activeSectionFilters);
        }
        
        if (!forSF && (freePIDCount <= 0))
        {
            return NULL;
        }
        
        if (DVBDemuxAllocateFilter(reader->adapter, pid))
        {
            if (reader->adapter->hardwareRestricted)
            {
                /* If this is a section filter and we are HW restricted fail straight away*/
                if (forSF)
                {
                    return NULL;
                }
                /* If this is not for a section filter and we are HW restricted try 
                 * and release a Section Filter PID filter so we can start this one.
                 */
                if (!forSF && (ListCount(reader->activeSectionFilters) > MIN_SECTION_FILTER_PIDS))
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

    if (!reader->adapter->hardwareRestricted && !reader->promiscuousMode && (pid == TSREADER_PID_ALL))
    {
        PromiscusModeEnable(reader, TRUE);
    }

    TSPacketFilterList_t *pfList = ObjectCreateType(TSPacketFilterList_t);
    pfList->pid = pid;
    pfList->filters = ListCreate();
    if (pid == TSREADER_PID_ALL)
    {
        reader->promiscuousPidFilters = pfList;
    }
    else
    {
        list = TSREADER_PIDFILTER_GETBUCKET(reader, pid);
        ListAdd(list, pfList);
    }
    return pfList;
}

static void PacketFilterListDestroy(TSReader_t *reader, TSPacketFilterList_t *pfList)
{
    if (!reader->promiscuousMode && (pfList->pid != TSREADER_PID_ALL))
    {
        DVBDemuxReleaseFilter(reader->adapter, pfList->pid);
    }
    if (pfList == reader->promiscuousPidFilters )
    {
        reader->promiscuousPidFilters = NULL;
    }
    else
    {
        List_t *list = TSREADER_PIDFILTER_GETBUCKET(reader, pfList->pid);
        ListRemove(list, pfList);
    }
    ListFree(pfList->filters, NULL);
    ObjectRefDec(pfList);
}

static TSPacketFilterList_t *PacketFilterListFind(TSReader_t *reader, uint16_t pid)
{
    ListIterator_t iterator;

    if (pid == TSREADER_PID_ALL)
    {
        return reader->promiscuousPidFilters;
    }
    
    for (ListIterator_Init(iterator, TSREADER_PIDFILTER_GETBUCKET(reader,pid)); 
            ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        TSPacketFilterList_t *sfList = ListIterator_Current(iterator);
        if (sfList->pid == pid)
        {
            return sfList;
        }
    }
    return NULL;
}

static bool PacketFilterListAddFilter(TSReader_t *reader, TSPacketFilter_t *packetFilter)
{
    TSPacketFilterList_t *pfList = PacketFilterListFind(reader, packetFilter->pid);
    if (pfList == NULL)
    {
        pfList = PacketFilterListCreate(reader, packetFilter->pid, packetFilter->group == NULL);
        if (pfList == NULL)
        {
            return FALSE;
        }
    }
    ListAdd(pfList->filters, packetFilter);
    return TRUE;
}

static void PacketFilterListRemoveFilter(TSReader_t *reader, TSPacketFilter_t *packetFilter)
{
    TSPacketFilterList_t *pfList = PacketFilterListFind(reader,packetFilter->pid);
    LogModule(LOG_DEBUG, TSREADER, "Removing packet filter %p on pid 0x%02x", packetFilter, packetFilter->pid);
    if (pfList)
    {
        ListRemove(pfList->filters, packetFilter);
        LogModule(LOG_DEBUG, TSREADER, "Packet filter list for pid 0x%02x now contains %d filters", packetFilter->pid, ListCount(pfList->filters));
        if ((ListCount(pfList->filters) == 0) && (!pthread_equal(reader->thread, pthread_self())))
        {
            PacketFilterListDestroy(reader, pfList);
            SectionFilterListScheduleFilters(reader);            
        }
    }
    else
    {
        LogModule(LOG_DEBUG, TSREADER, "Packet filter list not found for pid 0x%02x", packetFilter->pid);
    }
}

static TSSectionFilterList_t * SectionFilterListCreate(TSReader_t *reader, uint16_t pid)
{
    TSSectionFilterList_t *sfList = ObjectCreateType(TSSectionFilterList_t);
    sfList->pid = pid;
    sfList->filters = ListCreate();
    sfList->tsReader = reader;
    sfList->sectionHandle = dvbpsi_AttachSections(SectionFilterListPushSection, sfList);
    sfList->packetFilter = ObjectCreateType(TSPacketFilter_t);
    sfList->packetFilter->pid = sfList->pid;
    sfList->packetFilter->userArg = sfList;
    sfList->packetFilter->callback = SectionFilterListPacketCallback;
    ListAdd(reader->sectionFilters, sfList);
    return sfList;
}

static void SectionFilterListDestroy(TSReader_t *reader, TSSectionFilterList_t *sfList)
{
    if (ListRemove(reader->activeSectionFilters, sfList))
    {
        PacketFilterListRemoveFilter(reader, sfList->packetFilter);
    }
    else
    {
        ListRemove(reader->sectionFilters, sfList);
    }
    ListFree(sfList->filters, NULL);
    dvbpsi_DetachSections(sfList->sectionHandle);
    ObjectRefDec(sfList->packetFilter);
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
    for (ListIterator_Init(iterator, reader->sectionFilters); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        TSSectionFilterList_t *sfList = ListIterator_Current(iterator);
        sfList->flags |= TSSectFilterListFlags_PAYLOAD_START;
        if (!PacketFilterListAddFilter(reader, sfList->packetFilter))
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
    for (ListIterator_Init(iterator, reader->activeSectionFilters); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        TSSectionFilterList_t *sfList = ListIterator_Current(iterator);
        PacketFilterListRemoveFilter(sfList->tsReader, sfList->packetFilter);
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
        TSPacketFilterList_t *pfList = PacketFilterListFind(reader, sfList->pid);
        
        if (pfList && (ListCount(pfList->filters) == 1))
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
        PacketFilterListRemoveFilter(reader, toDeschedule->packetFilter);
        ListRemove(reader->activeSectionFilters, toDeschedule);
        ListAdd(reader->sectionFilters, toDeschedule);
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
        PacketFilterListRemoveFilter(sfList->tsReader, sfList->packetFilter);
        ListRemove(sfList->tsReader->activeSectionFilters, sfList);
        ListAdd(sfList->tsReader->sectionFilters, sfList);
        SectionFilterListScheduleFilters(sfList->tsReader);
    }
}

#define MAX_FDS 2
static void *FilterTS(void *arg)
{
    struct timeval now, last;
    int diff;
    unsigned long long prevpackets = 0;
    struct pollfd pfd[MAX_FDS];
    TSReader_t *state = (TSReader_t*)arg;
    DVBAdapter_t *adapter = state->adapter;
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
      
   
    
    while (!state->quit)
    {
        int p;
        int n;

        pfd[0].fd = state->notificationFds[0];
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
            pthread_mutex_lock(&state->mutex);
            state->notificationSent = FALSE;
            pthread_mutex_unlock(&state->mutex);
            i = read(state->notificationFds[0], &id, 1);
            
            if (state->quit)
            {
                break;
            }

            if (state->multiplexChanged)
            {
                InformMultiplexChanged(state);
                state->multiplexChanged = FALSE;
            }            
        }
        
        if (pfd[1].revents & POLLIN)
        {
            /* Read in packet */
            count = DVBDVRRead(adapter, (char*)readBuffer, readBufferSize, 1000);
            if (!adapter->frontEndLocked)
            {
                continue;
            }
            pthread_mutex_lock(&state->mutex);
            for (p = 0; (p < (count / TSPACKET_SIZE)) && state->enabled; p ++)
            {
                ProcessPacket(state, &readBuffer[p]);
                state->totalPackets ++;
                /* The structure of the transport stream has changed in a major way,
                    (ie new services, services removed) so inform all of the filters
                    that are interested.
                  */
                if (state->tsStructureChanged)
                {
                    InformTSStructureChanged(state);
                    state->tsStructureChanged = FALSE;
                }
            }
            for (p = 0; p < TSREADER_PID_ALL; p ++)
            {
                List_t *list = state->pidFilterBuckets[p];
                ListIterator_t iterator;
                ListIterator_ForEach(iterator, list)
                {
                    TSPacketFilterList_t *pfList = ListIterator_Current(iterator);
                    if (ListCount(pfList->filters) == 0)
                    {
                        PacketFilterListDestroy(state, pfList);
                    }
                }
            }
            gettimeofday(&now, 0);
            diff =(now.tv_sec - last.tv_sec) * 1000 + (now.tv_usec - last.tv_usec) / 1000;
            if (diff > 1000)
            {
                // Work out bit rates
                state->bitrate = (unsigned long)((state->totalPackets - prevpackets) * (188 * 8));
                prevpackets = state->totalPackets;
                last = now;
            }
            pthread_mutex_unlock(&state->mutex);
        }
        
    }
    ObjectFree(readBuffer);    
    LogModule(LOG_DEBUG, TSREADER, "Filter thread exiting.\n");
    return NULL;
}

static void ProcessPacket(TSReader_t *state, TSPacket_t *packet)
{
    uint16_t pid = TSPACKET_GETPID(*packet);
    TSPacketFilterList_t *pfList;
    if (!TSPACKET_ISVALID(*packet))
    {
        return;
    }
    pfList = PacketFilterListFind(state, pid);
    SendToPacketFilters(pfList, packet);
    SendToPacketFilters(state->promiscuousPidFilters, packet);        
}

static void SendToPacketFilters(TSPacketFilterList_t *pfList, TSPacket_t *packet)
{
    ListIterator_t iterator;
    if (pfList)
    {
        for (ListIterator_Init(iterator, pfList->filters); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
        {
            TSPacketFilter_t *filter = (TSPacketFilter_t*)ListIterator_Current(iterator);
            if (filter->group)
            {
                filter->group->packetsProcessed ++;
            }
            filter->callback(filter->userArg, filter->group, packet);
        }
    }
}

static void InformTSStructureChanged(TSReader_t *state)
{
    ListIterator_t iterator;
    for (ListIterator_Init(iterator, state->groups); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        TSFilterGroup_t *group =(TSFilterGroup_t *)ListIterator_Current(iterator);
        if (group->eventCallback)
        {
            group->eventCallback(group->userArg, group, TSFilterEventType_StructureChanged, NULL);
        }
    }
}

static void InformMultiplexChanged(TSReader_t *state)
{
    ListIterator_t iterator;
    for (ListIterator_Init(iterator, state->groups); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        TSFilterGroup_t *group =(TSFilterGroup_t *)ListIterator_Current(iterator);
        if (group->eventCallback)
        {
            group->eventCallback(group->userArg, group, TSFilterEventType_MuxChanged, state->multiplex);
        }
    }
}
