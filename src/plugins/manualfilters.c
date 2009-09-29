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
    {\
        TSFilterGroup_t *group = TSReaderFindFilterGroup(MainTSReaderGet(), (_name), ManualPIDFilterType);\
        if (!group)\
        {\
            CommandError(COMMAND_ERROR_GENERIC, "Manual filter not found!");\
            return;\
        }\
        filter = group->userArg;\
    }

/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/
typedef struct ManualFilter_s{
    TSFilterGroup_t *tsgroup;
    DeliveryMethodInstance_t *dmInstance;
}ManualFilter_t;

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
static void OutputPacket(void *userArg, TSFilterGroup_t *group, TSPacket_t *packet);
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
        2, 2,
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
        1, 1,
        "Remove a destination for manually filtered PIDs.",
        "rmoutput <filter name>\n"
        "Removes the destination and stops all filters associated with this output.",
        CommandRemoveMF
    },
    {
        "lsmfs",
        0, 0,
        "List current filters.",
        "List all active additonal output names and destinations.",
        CommandListMF
    },
    {
        "setmfmrl",
        2, 2,
        "Set the filter's MRL.",
        "setmfmrl <filter name> <mrl>\n"
        "Change the destination for packets sent to this output. If the MRL cannot be"
        " parsed no change will be made to the output.",
        CommandSetOutputMRL
    },
    {
        "addmfpid",
        2, 2,
        "Adds a PID to a filter.",
        "addmfpid <filter name> <pid>\n"
        "Adds a PID to the filter to be sent to the specified output. The PID can be "
        "specified in either hex (starting with 0x) or decimal format.",
        CommandAddMFPID
    },
    {
        "rmmfpid",
        2, 2,
        "Removes a PID from a filter.",
        "rmmfpid <filter name> <pid>\n"
        "Removes the PID from the filter that is sending packets to the specified output."
        "The PID can be specified in either hex (starting with 0x) or decimal format.",
        CommandRemoveMFPID
    },
    {
        "lsmfpids",
        1, 1,
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
    TSReader_t *tsReader = MainTSReaderGet();
    TSFilterGroup_t *group;
    ManualFilter_t *filter;

    CommandCheckAuthenticated();

    group = TSReaderFindFilterGroup(tsReader, argv[0], ManualPIDFilterType);
    if (group)
    {
        CommandError(COMMAND_ERROR_GENERIC, "A manual filter with this name exists!");
        return;
    }
    ObjectRegisterType(ManualFilter_t);
    filter = ObjectCreateType(ManualFilter_t);
    if (!filter)
    {
        CommandError(COMMAND_ERROR_GENERIC, "Failed to allocate a filter!");
        return;
    }

    filter->dmInstance = DeliveryMethodCreate(argv[1]);
    if (filter->dmInstance == NULL)
    {
        filter->dmInstance = DeliveryMethodCreate("null://");
    }

    pthread_mutex_lock(&manualFiltersMutex);
    filter->tsgroup = TSReaderCreateFilterGroup(tsReader, strdup(argv[0]), ManualPIDFilterType, NULL, filter);
    if (!filter->tsgroup)
    {
        DeliveryMethodDestroy(filter->dmInstance);
        ObjectRefDec(filter);
        CommandError(COMMAND_ERROR_GENERIC, "Failed to allocate a filter!");
    }
    pthread_mutex_unlock(&manualFiltersMutex);
}

static void CommandRemoveMF(int argc, char **argv)
{
    ManualFilter_t *filter;
    char *name;

    CommandCheckAuthenticated();

    FIND_MANUAL_FILTER(argv[0]);

    pthread_mutex_lock(&manualFiltersMutex);
    name = filter->tsgroup->name;
    TSFilterGroupDestroy(filter->tsgroup);
    pthread_mutex_unlock(&manualFiltersMutex);
    
    free(name);
    DeliveryMethodDestroy(filter->dmInstance);
    ObjectRefDec(filter);
}

static void CommandListMF(int argc, char **argv)
{
    TSReader_t *tsReader = MainTSReaderGet();
    ListIterator_t iterator;
    pthread_mutex_lock(&manualFiltersMutex);
    for ( ListIterator_Init(iterator, tsReader->groups);
          ListIterator_MoreEntries(iterator);
          ListIterator_Next(iterator))
    {
        TSFilterGroup_t *group = (TSFilterGroup_t *)ListIterator_Current(iterator);
        if (strcmp(group->type, ManualPIDFilterType) == 0)
        {
            CommandPrintf("%10s : %s\n", group->name,  DeliveryMethodGetMRL(group->userArg));
        }
    }
    pthread_mutex_unlock(&manualFiltersMutex);
}

static void CommandSetOutputMRL(int argc, char **argv)
{
    ManualFilter_t *filter;
    DeliveryMethodInstance_t *instance;

    CommandCheckAuthenticated();

    FIND_MANUAL_FILTER(argv[0]);
    pthread_mutex_lock(&manualFiltersMutex);

    instance = DeliveryMethodCreate(argv[1]);
    if (instance)
    {
        pthread_mutex_lock(&manualFiltersMutex);
        DeliveryMethodDestroy(filter->dmInstance);
        filter->dmInstance = instance;
        pthread_mutex_unlock(&manualFiltersMutex);
        CommandPrintf("MRL set to \"%s\" for %s\n", DeliveryMethodGetMRL(filter->dmInstance), argv[0]);
    }
    else
    {
        CommandError(COMMAND_ERROR_GENERIC,"Failed to set MRL");
    }
    pthread_mutex_unlock(&manualFiltersMutex);
}
static void CommandAddMFPID(int argc, char **argv)
{
    ManualFilter_t *filter;
    int pid;
    CommandCheckAuthenticated();

    FIND_MANUAL_FILTER(argv[0]);

    pid = ParsePID(argv[1]);
    if ((pid < 0) || (pid > 0x2000))
    {
        CommandError(COMMAND_ERROR_GENERIC, "Invalid PID!");
        return;
    }
    pthread_mutex_lock(&manualFiltersMutex);    
    if (TSFilterGroupAddPacketFilter(filter->tsgroup, pid, OutputPacket, filter))
    {
        CommandError(COMMAND_ERROR_GENERIC,"No more available PID entries!");
    }
    pthread_mutex_unlock(&manualFiltersMutex);

}

static void CommandRemoveMFPID(int argc, char **argv)
{
    ManualFilter_t *filter;
    int pid;
    CommandCheckAuthenticated();

    FIND_MANUAL_FILTER(argv[0]);

    pid = ParsePID(argv[1]);
    if ((pid < 0) || (pid > 0x2000))
    {
        CommandError(COMMAND_ERROR_GENERIC, "Invalid PID!");
        return;
    }
    pthread_mutex_lock(&manualFiltersMutex);
    TSFilterGroupRemovePacketFilter(filter->tsgroup, pid);
    pthread_mutex_unlock(&manualFiltersMutex);
}

static void CommandListMFPIDs(int argc, char **argv)
{
    ManualFilter_t *filter;
    TSPacketFilter_t *packetFilter;
    int count = 0;

    FIND_MANUAL_FILTER(argv[0]);
    pthread_mutex_lock(&manualFiltersMutex);
    for (packetFilter=filter->tsgroup->packetFilters; packetFilter; packetFilter=packetFilter->next)
    {
        count ++;
    }

    CommandPrintf("%d PIDs for \'%s\'\n", count, argv[0]);

    for (packetFilter=filter->tsgroup->packetFilters; packetFilter; packetFilter=packetFilter->next)
    {
        CommandPrintf("0x%x\n", packetFilter->pid);
    }
    pthread_mutex_unlock(&manualFiltersMutex);
}

/*******************************************************************************
* Helper Functions                                                             *
*******************************************************************************/
static void OutputPacket(void *userArg, TSFilterGroup_t *group, TSPacket_t *packet)
{
    ManualFilter_t *filter = userArg;
    DeliveryMethodOutputPacket(filter->dmInstance, packet);
}

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

