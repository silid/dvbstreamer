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

pids.c

Manage PIDs.

*/
#include <stdlib.h>
#include <string.h>
#include "dbase.h"
#include "multiplexes.h"
#include "services.h"
#include "pids.h"
#include "logging.h"
/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/
#define SPECIAL_PID_PMT 0x2001
#define SPECIAL_PID_PCR 0x8000
/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/

static void *RollUpDescriptors(dvbpsi_descriptor_t *descriptors, int *datasize);
static dvbpsi_descriptor_t *UnRollDescriptors(uint8_t *descriptors, int size);
static int PIDListCount(Service_t *service);
static int PIDAdd(Service_t *service, StreamInfo_t *pid);

static void ProgramInfoDestructor(void *ptr);
static void StreamInfoListDestructor(void *ptr);
/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static bool typeInited = FALSE;
/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/

ProgramInfo_t *ProgramInfoNew(int nrofStreams)
{
    ProgramInfo_t *info;
    if (!typeInited)
    {
        ObjectRegisterTypeDestructor(ProgramInfo_t, ProgramInfoDestructor);
        ObjectRegisterCollection(TOSTRING(StreamInfoList_t), sizeof(StreamInfo_t), StreamInfoListDestructor);
        typeInited = TRUE;
    }
    info = ObjectCreateType(ProgramInfo_t);
    if (info)
    {
        info->streamInfoList =(StreamInfoList_t *)ObjectCollectionCreate(TOSTRING(StreamInfoList_t), nrofStreams);
    }
    return info;
}


int ProgramInfoSet(Service_t *service, ProgramInfo_t *info)
{
    int rc = 0;
    int i;
    StreamInfo_t pid;

    for (i = 0; i < info->streamInfoList->nrofStreams; i ++)
    {
        rc = PIDAdd(service, &info->streamInfoList->streams[i]);
        if (rc)
        {
            break;
        }
    }
    pid.pid = SPECIAL_PID_PCR | (info->pcrPID & PID_MASK); 
    pid.type = 0;
    pid.descriptors = info->descriptors;
    PIDAdd(service, &pid);
    return rc;
}

ProgramInfo_t *ProgramInfoGet(Service_t *service)
{
    int count = PIDListCount(service);
    ProgramInfo_t *result = NULL;

    if (count > 0)
    {
        result = ProgramInfoNew(count);
        if (result)
        {
            int i;
            STATEMENT_INIT;
            STATEMENT_PREPAREVA("SELECT "
                        PID_PID ","
                        PID_TYPE ","
                        PID_DESCRIPTORS " "
                        "FROM " PIDS_TABLE " WHERE " PID_MULTIPLEXUID "=%d AND " PID_SERVICEID "=%d AND "
                        PID_PID "<8192;",
                        service->multiplexUID, service->id);
            if (rc == SQLITE_OK)
            {
                for (i = 0; i < count; i ++)
                {
                    STATEMENT_STEP();
                    if (rc == SQLITE_ROW)
                    {
                        result->streamInfoList->streams[i].pid = STATEMENT_COLUMN_INT( 0);
                        result->streamInfoList->streams[i].type = STATEMENT_COLUMN_INT( 1);
                        result->streamInfoList->streams[i].descriptors = UnRollDescriptors((uint8_t *)sqlite3_column_blob(stmt, 2), 
                                                                        sqlite3_column_bytes(stmt, 2));
                    }
                    else
                    {
                        break;
                    }
                }

                STATEMENT_FINALIZE();
                
                STATEMENT_PREPAREVA("SELECT "
                        PID_PID ","
                        PID_DESCRIPTORS " "
                        "FROM " PIDS_TABLE " WHERE " PID_MULTIPLEXUID "=%d AND " PID_SERVICEID "=%d AND "
                        PID_PID ">%d;",
                        service->multiplexUID, service->id, SPECIAL_PID_PCR);
                if (rc == SQLITE_OK)
                {
                    STATEMENT_STEP();
                    if (rc == SQLITE_ROW)
                    {
                        result->pcrPID = STATEMENT_COLUMN_INT( 0) & PID_MASK;
                        result->descriptors = UnRollDescriptors((uint8_t *)sqlite3_column_blob(stmt, 1), 
                                                                        sqlite3_column_bytes(stmt, 1));
                    }
                    STATEMENT_FINALIZE();
                }
            }
            else
            {
                ObjectRefDec(result);
                result = NULL;
            }
        }
    }
    return result;
}


int ProgramInfoRemove(Service_t *service)
{
    STATEMENT_INIT;

    STATEMENT_PREPAREVA("DELETE FROM " PIDS_TABLE " "
                        "WHERE " PID_MULTIPLEXUID "=%d AND " PID_SERVICEID "=%d;",
                        service->multiplexUID, service->id);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();

    STATEMENT_FINALIZE();
    return 0;
}

/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/
static int PIDListCount(Service_t *service)
{
    STATEMENT_INIT;
    int result = -1;

    STATEMENT_PREPAREVA("SELECT count () FROM " PIDS_TABLE " "
                        "WHERE " PID_MULTIPLEXUID "=%d AND " PID_SERVICEID "=%d AND " PID_PID "<8192;",
                        service->multiplexUID, service->id);
    RETURN_ON_ERROR(-1);

    STATEMENT_STEP();
    if (rc == SQLITE_ROW)
    {
        result = STATEMENT_COLUMN_INT( 0);
        rc = 0;
    }
    STATEMENT_FINALIZE();
    return result;
}

static int PIDAdd(Service_t *service, StreamInfo_t *pid)
{
    void *descriptorblob;
    int size;
    STATEMENT_INIT;

    STATEMENT_PREPAREVA("INSERT INTO " PIDS_TABLE " (" PID_MULTIPLEXUID ","
                        PID_SERVICEID ","       
                        PID_PID ","
                        PID_TYPE ","
                        PID_DESCRIPTORS ") "
                        "VALUES (%d,%d,%d,%d,?);",
                        service->multiplexUID, service->id,
                        pid->pid, pid->type);
    RETURN_RC_ON_ERROR;
    descriptorblob = RollUpDescriptors(pid->descriptors, &size);
    sqlite3_bind_blob(stmt, 1, descriptorblob, size, free);

    STATEMENT_STEP();
    STATEMENT_FINALIZE();
    return rc;
}

static void *RollUpDescriptors(dvbpsi_descriptor_t *descriptors, int *datasize)
{
    uint8_t *result;
    int size = 0;
    int pos = 0;
    dvbpsi_descriptor_t *current = descriptors;

    while (current)
    {
        size += current->i_length + 2;
        current = current->p_next;
    }

    if (size == 0)
    {
        *datasize = 0;
        return NULL;
    }

    result = malloc(size);
    if (!result)
    {
        LogModule(LOG_ERROR, "PIDS", "Failed to allocate memory to roll up descriptors! (size %d)\n", size );
        *datasize = 0;
        return NULL;
    }
    current = descriptors;

    while (current)
    {
        result[pos    ] = current->i_tag;
        result[pos + 1] = current->i_length;
        memcpy(&result[pos + 2], current->p_data, current->i_length);
        pos += current->i_length + 2;
        current = current->p_next;
    }
    *datasize = size;
    return result;
}

static dvbpsi_descriptor_t *UnRollDescriptors(uint8_t *descriptors, int size)
{
    dvbpsi_descriptor_t *result = NULL;
    dvbpsi_descriptor_t *current = NULL, *prev=NULL;
    int pos;

    if (size == 0)
    {
        return NULL;
    }

    for (pos = 0; pos < size; pos += 2 + current->i_length)
    {
        current = dvbpsi_NewDescriptor(descriptors[pos], descriptors[pos + 1], &descriptors[pos + 2]);
        if (result)
        {
            prev->p_next = current;
            prev = current;
        }
        else
        {
            prev = result = current;
        }
    }

    return result;
}

static void ProgramInfoDestructor(void *ptr)
{
    ProgramInfo_t *info = ptr;
    if (info->descriptors)
    {
        dvbpsi_DeleteDescriptors(info->descriptors);
    }
    ObjectRefDec(info->streamInfoList);
}

static void StreamInfoListDestructor(void *ptr)
{
    StreamInfoList_t *list = ptr;
    int i;
    
    for (i = 0; i < list->nrofStreams; i ++)
    {
        if (list->streams[i].descriptors)
        {
            dvbpsi_DeleteDescriptors(list->streams[i].descriptors);
        }
    }
}

