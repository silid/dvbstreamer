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

static void *RollUpDescriptors(dvbpsi_descriptor_t *descriptors, int *datasize);
static dvbpsi_descriptor_t *UnRollDescriptors(char *descriptors, int size);
static int PIDAdd(Service_t *service, PID_t *pid);

PIDList_t *PIDListNew(int count)
{
    PIDList_t *result = ObjectAlloc(sizeof(PIDList_t) + (sizeof(PID_t) * count));
    if (result)
    {
        result->count = count;
    }
    return result;
}

void PIDListFree(PIDList_t *pids)
{
    int i;
    if (pids)
    {
        for (i = 0; i < pids->count; i ++)
        {
            if (pids->pids[i].descriptors)
            {
                dvbpsi_DeleteDescriptors(pids->pids[i].descriptors);
            }
        }
        ObjectFree(pids);
    }
}

int PIDListSet(Service_t *service, PIDList_t *pids)
{
    int rc;
    int i;

    for (i = 0; i < pids->count; i ++)
    {
        rc = PIDAdd(service, &pids->pids[i]);
        if (rc)
        {
            break;
        }
    }
    return rc;
}

PIDList_t *PIDListGet(Service_t *service)
{
    int count = PIDListCount(service);
    PIDList_t *result = NULL;

    if (count > 0)
    {
        result = PIDListNew(count);
        if (result)
        {
            int i;
            STATEMENT_INIT;
            STATEMENT_PREPAREVA("SELECT "
                        PID_PID ","
                        PID_TYPE ","
                        PID_SUBTYPE ","
                        PID_PMTVERSION ","
                        PID_DESCRIPTORS " "
                        "FROM " PIDS_TABLE " WHERE " PID_MPLEXFREQ "=%d AND " PID_SERVICEID "=%d;",
                        service->multiplexFreq, service->id);
            if (rc == SQLITE_OK)
            {

                for (i = 0; i < count; i ++)
                {
                    STATEMENT_STEP();
                    if (rc == SQLITE_ROW)
                    {
                        result->pids[i].pid = STATEMENT_COLUMN_INT( 0);
                        result->pids[i].type = STATEMENT_COLUMN_INT( 1);
                        result->pids[i].subType = STATEMENT_COLUMN_INT( 2);
                        result->pids[i].pmtVersion = STATEMENT_COLUMN_INT( 3);
                        result->pids[i].descriptors = UnRollDescriptors((char *)sqlite3_column_blob(stmt, 4),sqlite3_column_bytes(stmt, 4));
                    }
                    else
                    {
                        break;
                    }
                }
                STATEMENT_FINALIZE();
            }
            else
            {
                PIDListFree(result);
                result = NULL;
            }
        }
    }
    return result;
}

int PIDListCount(Service_t *service)
{
    STATEMENT_INIT;
    int result = -1;

    STATEMENT_PREPAREVA("SELECT count () FROM " PIDS_TABLE " "
                        "WHERE " PID_MPLEXFREQ "=%d AND " PID_SERVICEID "=%d;",
                        service->multiplexFreq, service->id);
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

int PIDListRemove(Service_t *service)
{
    STATEMENT_INIT;

    STATEMENT_PREPAREVA("DELETE FROM " PIDS_TABLE " "
                        "WHERE " PID_MPLEXFREQ "=%d AND " PID_SERVICEID "=%d;",
                        service->multiplexFreq, service->id);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();

    STATEMENT_FINALIZE();
    return 0;
}


static int PIDAdd(Service_t *service, PID_t *pid)
{
    void *descriptorblob;
    int size;
    STATEMENT_INIT;

    STATEMENT_PREPAREVA("INSERT INTO " PIDS_TABLE " "
                        "VALUES (%d,%d,%d,%d,%d,%d,?);",
                        service->multiplexFreq, service->id,
                        pid->pid, pid->type, pid->subType, service->pmtVersion);
    RETURN_RC_ON_ERROR;

    descriptorblob = RollUpDescriptors(pid->descriptors, &size);
    sqlite3_bind_blob(stmt, 1, descriptorblob, size, free);

    STATEMENT_STEP();
    STATEMENT_FINALIZE();
    return rc;
}

static void *RollUpDescriptors(dvbpsi_descriptor_t *descriptors, int *datasize)
{
    char *result;
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
        printlog(LOG_ERROR, "Failed to allocate memory to roll up descriptors! (size %d)\n", size );
        *datasize = 0;
        return NULL;
    }
    current = descriptors;

    while (current)
    {
        result[pos    ] = current->i_tag;
        result[pos + 1] = current->i_length;
        memcpy(result + 2, current->p_data, current->i_length);
        pos += current->i_length + 2;
        current = current->p_next;
    }
    *datasize = size;
    return result;
}

static dvbpsi_descriptor_t *UnRollDescriptors(char *descriptors, int size)
{
    dvbpsi_descriptor_t *result = NULL;
    dvbpsi_descriptor_t *current = NULL;
    int pos;

    if (size == 0)
    {
        return NULL;
    }

    for (pos = 0; pos < size; pos += 2 + current->i_length)
    {
        if (current)
        {
            current->p_next = calloc(1, sizeof(dvbpsi_descriptor_t));
            current = current->p_next;
        }
        else
        {
            current = calloc(1, sizeof(dvbpsi_descriptor_t));
            result = current;
        }
        current->i_tag    = descriptors[pos];
        current->i_length = descriptors[pos + 1];
    }

    return result;
}
