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

manualfilters.c

Plugin to allow manual filtering of PIDs.

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
#include "deliverymethod.h"
#include "ts.h"


/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/
#define FIND_MANUAL_FILTER(_name) \
    filter = TSFilterFindPIDFilter(MainTSFilterGet(), (_name), ManualPIDFilterType);\
    if (!filter)\
    {\
        CommandError(COMMAND_ERROR_GENERIC, "Manual filter not found!");\
        return;\
    }

/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/


/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void CommandAddMF(int argc, char **argv);
static void CommandRemoveMF(int argc, char **argv);
static void CommandListMF(int argc, char **argv);
static void CommandSetOutputMRL(int argc, char **argv);
static void CommandAddMFPID(int argc, char **argv);
static void CommandRemoveMFPID(int argc, char **argv);
static void CommandListMFPIDs(int argc, char **argv);

static int ParsePID(char *argument);


/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
/* For log output (not used yet)
static char MANUALFILTER[]="ManualFilter";
*/
static char ManualPIDFilterType[] = "Manual";
static pthread_mutex_t manualFiltersMutex = PTHREAD_MUTEX_INITIALIZER;

/*******************************************************************************
* Plugin Setup                                                                 *
*******************************************************************************/

PLUGIN_COMMANDS(
    {
        "addmf",
        TRUE, 2, 2,
        "Add a new destination for manually filtered PIDs.",
        "addmf <filter name> <mrl>\n"
        "Adds a new destination for sending packets to. This is only used for "
        "manually filtered packets. "
        "To send packets to this destination you'll need to also call \'addmfpid\' "
        "with this output as an argument.",
        CommandAddMF
    },
    {
        "rmmf",
        TRUE, 1, 1,
        "Remove a destination for manually filtered PIDs.",
        "rmoutput <filter name>\n"
        "Removes the destination and stops all filters associated with this output.",
        CommandRemoveMF
    },
    {
        "lsmfs",
        FALSE, 0, 0,
        "List current filters.",
        "List all active additonal output names and destinations.",
        CommandListMF
    },
    {
        "setmfmrl",
        TRUE, 2, 2,
        "Set the filter's MRL.",
        "setmfmrl <filter name> <mrl>\n"
        "Change the destination for packets sent to this output. If the MRL cannot be"
        " parsed no change will be made to the output.",
        CommandSetOutputMRL
    },
    {
        "addmfpid",
        TRUE, 2, 2,
        "Adds a PID to a filter.",
        "addmfpid <filter name> <pid>\n"
        "Adds a PID to the filter to be sent to the specified output. The PID can be "
        "specified in either hex (starting with 0x) or decimal format.",
        CommandAddMFPID
    },
    {
        "rmmfpid",
        TRUE, 2, 2,
        "Removes a PID from a filter.",
        "rmmfpid <filter name> <pid>\n"
        "Removes the PID from the filter that is sending packets to the specified output."
        "The PID can be specified in either hex (starting with 0x) or decimal format.",
        CommandRemoveMFPID
    },
    {
        "lsmfpids",
        TRUE, 1, 1,
        "List PIDs for filter.",
        "lsmfpids <filter name>\n"
        "List the PIDs being filtered for a specific output.",
        CommandListMFPIDs
    }
);

PLUGIN_INTERFACE_C(
    PLUGIN_FOR_ALL,
    "ManualFilter", "0.1",
    "Plugin to allow manual filtering of PID.",
    "charrea6@users.sourceforge.net"
    );

/*******************************************************************************
* Command Functions                                                            *
*******************************************************************************/
static void CommandAddMF(int argc, char **argv)
{
    DVBAdapter_t *adapter = MainDVBAdapterGet();
    TSFilter_t *tsFilter = MainTSFilterGet();
    PIDFilter_t *filter;
    PIDFilterSimpleFilter_t *simplePIDFilter;
    DeliveryMethodInstance_t *dmInstance;

    if (adapter->hardwareRestricted)
    {
        CommandError(COMMAND_ERROR_GENERIC, "Not supported in hardware restricted mode!");
        return;
    }
    CommandCheckAuthenticated();

    filter = TSFilterFindPIDFilter(tsFilter, argv[0], ManualPIDFilterType);
    if (filter)
    {
        CommandError(COMMAND_ERROR_GENERIC, "A manual filter with this name exists!");
        return;
    }

    filter = PIDFilterAllocate(tsFilter);
    if (!filter)
    {
        CommandError(COMMAND_ERROR_GENERIC, "Failed to allocate a filter!");
        return;
    }

    simplePIDFilter =(PIDFilterSimpleFilter_t*)ObjectAlloc(sizeof(PIDFilterSimpleFilter_t));
    if (!simplePIDFilter)
    {
        CommandError(COMMAND_ERROR_GENERIC, "Failed to allocated PIDFilterSimpleFilter_t structure!");
        PIDFilterFree(filter);
        return;
    }
    dmInstance = DeliveryMethodCreate(argv[1]);
    if (dmInstance == NULL)
    {
        dmInstance = DeliveryMethodCreate("null://");
    }

    PIDFilterFilterPacketSet(filter, PIDFilterSimpleFilter, simplePIDFilter);
    PIDFilterOutputPacketSet(filter, DeliveryMethodOutputPacket, dmInstance);

    filter->name = strdup(argv[0]);
    filter->type = ManualPIDFilterType;
    filter->enabled = TRUE;
}

static void CommandRemoveMF(int argc, char **argv)
{
    PIDFilter_t *filter;
    PIDFilterSimpleFilter_t *simplePIDFilter;
    DeliveryMethodInstance_t *deliveryMethod;
    char *name;

    CommandCheckAuthenticated();

    FIND_MANUAL_FILTER(argv[0]);

    name = filter->name;
    simplePIDFilter = filter->fpArg;
    deliveryMethod = filter->opArg;
    PIDFilterFree(filter);
    ObjectRefDec(simplePIDFilter);
    free(name);
    DeliveryMethodDestroy(deliveryMethod);
}

static void CommandListMF(int argc, char **argv)
{
    TSFilter_t *tsFilter = MainTSFilterGet();
    ListIterator_t iterator;

    TSFilterLock(tsFilter);
    for ( ListIterator_Init(iterator, tsFilter->pidFilters);
          ListIterator_MoreEntries(iterator);
          ListIterator_Next(iterator))
    {
        PIDFilter_t *filter = (PIDFilter_t *)ListIterator_Current(iterator);
        if (strcmp(filter->type, ManualPIDFilterType) == 0)
        {
            CommandPrintf("%10s : %s\n", filter->name,  DeliveryMethodGetMRL(filter));
        }
    }
    TSFilterUnLock(tsFilter);
}

static void CommandSetOutputMRL(int argc, char **argv)
{
    PIDFilter_t *filter;
    DeliveryMethodInstance_t *instance;

    CommandCheckAuthenticated();

    FIND_MANUAL_FILTER(argv[0]);

    pthread_mutex_lock(&manualFiltersMutex);
    instance = DeliveryMethodCreate(argv[1]);
    if (instance)
    {
        DeliveryMethodDestroy(filter->opArg);
        filter->opArg = instance;
        CommandPrintf("MRL set to \"%s\" for %s\n", DeliveryMethodGetMRL(filter), argv[0]);
    }
    else
    {
        CommandError(COMMAND_ERROR_GENERIC,"Failed to set MRL");
    }
    pthread_mutex_unlock(&manualFiltersMutex);
}
static void CommandAddMFPID(int argc, char **argv)
{
    PIDFilter_t *filter;

    CommandCheckAuthenticated();

    FIND_MANUAL_FILTER(argv[0]);

    if (filter)
    {
        int pid;
        PIDFilterSimpleFilter_t *simplePIDFilter;
        pid = ParsePID(argv[1]);
        if ((pid < 0) || (pid > 0x2000))
        {
            CommandError(COMMAND_ERROR_GENERIC, "Invalid PID!");
            return;
        }
        pthread_mutex_lock(&manualFiltersMutex);
        simplePIDFilter = filter->fpArg;
        if (simplePIDFilter->pidcount == MAX_PIDS)
        {
            CommandError(COMMAND_ERROR_GENERIC,"No more available PID entries!");
        }
        else
        {
            simplePIDFilter->pids[simplePIDFilter->pidcount] = pid;
            simplePIDFilter->pidcount ++;
        }
        pthread_mutex_unlock(&manualFiltersMutex);
    }
}

static void CommandRemoveMFPID(int argc, char **argv)
{
    PIDFilter_t *filter;

    CommandCheckAuthenticated();

    FIND_MANUAL_FILTER(argv[0]);

    if (filter)
    {
        int pid, i;
        PIDFilterSimpleFilter_t *simplePIDFilter;

        pid = ParsePID(argv[1]);
        if ((pid < 0) || (pid > 0x2000))
        {
            CommandError(COMMAND_ERROR_GENERIC, "Invalid PID!");
            return;
        }

        pthread_mutex_lock(&manualFiltersMutex);
        simplePIDFilter = filter->fpArg;
        for ( i = 0; i < simplePIDFilter->pidcount; i ++)
        {
            if (simplePIDFilter->pids[i] == pid)
            {
                memcpy(&simplePIDFilter->pids[i], &simplePIDFilter->pids[i + 1],
                       (simplePIDFilter->pidcount - (i + 1)) * sizeof(uint16_t));
                simplePIDFilter->pidcount --;
                break;
            }
        }
        pthread_mutex_unlock(&manualFiltersMutex);

    }
}

static void CommandListMFPIDs(int argc, char **argv)
{
    int i;
    PIDFilter_t *filter;
    PIDFilterSimpleFilter_t *simplePIDFilter;

    FIND_MANUAL_FILTER(argv[0]);
    pthread_mutex_lock(&manualFiltersMutex);
    simplePIDFilter = filter->fpArg;

    CommandPrintf("%d PIDs for \'%s\'\n", simplePIDFilter->pidcount, argv[0]);

    for (i = 0; i < simplePIDFilter->pidcount; i ++)
    {
        CommandPrintf("0x%x\n", simplePIDFilter->pids[i]);
    }
    pthread_mutex_unlock(&manualFiltersMutex);

}

/*******************************************************************************
* Helper Functions                                                             *
*******************************************************************************/
static int ParsePID(char *argument)
{
    char *formatstr;
    int pid = 0;

    if ((argument[0] == '0') && (tolower(argument[1])=='x'))
    {
        argument[1] = 'x';
        formatstr = "0x%x";
    }
    else
    {
        formatstr = "%d";
    }

    if (sscanf(argument, formatstr, &pid) == 0)
    {
        return -1;
    }

    return pid;
}

