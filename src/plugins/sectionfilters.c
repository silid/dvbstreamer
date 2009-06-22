/*
Copyright (C) 2009  Adam Charrett

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

sectionfilters.c

Plugin to allow manual filtering of sections.

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
#define FIND_SECTION_FILTER(_name) \
    do { \
        int pid; \
        char filterName[20]; /* Section(PID 0x%04x) */ \
        pid = ParsePID(_name); \
        sprintf(filterName, SectionFilterNameFormat, pid); \
        filter = TSFilterFindPIDFilter(MainTSFilterGet(), (filterName), SectionPIDFilterType);\
        if (!filter)\
        {\
            CommandError(COMMAND_ERROR_GENERIC, "Section filter not found!");\
            return;\
        }\
    }while(0)

/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/
typedef struct SectionFilter_s
{
    DeliveryMethodInstance_t *dmInstance;
    dvbpsi_handle dvbpsiHandle;
}SectionFilter_t;

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void CommandAddSecF(int argc, char **argv);
static void CommandRemoveSecF(int argc, char **argv);
static void CommandListSecF(int argc, char **argv);
static void CommandSetOutputMRL(int argc, char **argv);

static TSPacket_t * SectionFilterProcessPacket(PIDFilter_t *pidfilter, void *arg, TSPacket_t *packet);
static void SectionFilterGatherSections(dvbpsi_decoder_t* p_decoder, dvbpsi_psi_section_t* p_section);

static int ParsePID(char *argument);


/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
/* For log output (not used yet)
static char SECTIONFILTER[]="SectionFilter";
*/
static char SectionPIDFilterType[] = "Section";
static char SectionFilterNameFormat[] = "Section(PID 0x%04x)";
static pthread_mutex_t sectionFiltersMutex = PTHREAD_MUTEX_INITIALIZER;

/*******************************************************************************
* Plugin Setup                                                                 *
*******************************************************************************/

PLUGIN_COMMANDS(
    {
        "addsecf",
        2, 2,
        "Add a new section filter for a PID.",
        "addsecf <pid> <mrl>\n"
        "Adds a new section filter for the specified PID.",
        CommandAddSecF
    },
    {
        "rmsecf",
        1, 1,
        "Remove a section filter.",
        "rmoutput <pid>\n"
        "Stops and removes the section filter for the specified PID.",
        CommandRemoveSecF
    },
    {
        "lssecfs",
        0, 0,
        "List sections filters.",
        "List all active section filters.",
        CommandListSecF
    },
    {
        "setsecfmrl",
        2, 2,
        "Set the filter's MRL.",
        "setmfmrl <pid> <mrl>\n"
        "Change the destination for sections sent to this output. If the MRL cannot be"
        " parsed no change will be made to the output.",
        CommandSetOutputMRL
    }    
);

PLUGIN_INTERFACE_C(
    PLUGIN_FOR_ALL,
    "SectionFilter", "0.1",
    "Plugin to allow filtering of sections for a PID.",
    "charrea6@users.sourceforge.net"
    );

/*******************************************************************************
* Command Functions                                                            *
*******************************************************************************/
static void CommandAddSecF(int argc, char **argv)
{
    DVBAdapter_t *adapter = MainDVBAdapterGet();
    TSFilter_t *tsFilter = MainTSFilterGet();
    PIDFilter_t *filter;
    PIDFilterSimpleFilter_t *simplePIDFilter;
    DeliveryMethodInstance_t *dmInstance;
    uint16_t pid;
    char filterName[20]; /* Section(PID 0x%04x) */
    dvbpsi_decoder_t* dvbpsiHandle;
    SectionFilter_t *sectionFilter;
    
    if (adapter->hardwareRestricted)
    {
        CommandError(COMMAND_ERROR_GENERIC, "Not supported in hardware restricted mode!");
        return;
    }
    CommandCheckAuthenticated();

    pid = ParsePID(argv[0]);
    sprintf(filterName, SectionFilterNameFormat, pid);
        
    filter = TSFilterFindPIDFilter(tsFilter, filterName, SectionPIDFilterType);
    if (filter)
    {
        CommandError(COMMAND_ERROR_GENERIC, "Already section filtering this PID!");
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
        CommandError(COMMAND_ERROR_GENERIC, "Failed to allocate PIDFilterSimpleFilter_t structure!");
        PIDFilterFree(filter);
        return;
    }
    simplePIDFilter->pidcount = 1;
    simplePIDFilter->pids[0] = pid;
    
    dmInstance = DeliveryMethodCreate(argv[1]);
    if (dmInstance == NULL)
    {
        dmInstance = DeliveryMethodCreate("null://");
    }

    ObjectRegisterType(SectionFilter_t);
    sectionFilter = ObjectCreateType(SectionFilter_t);
    if (sectionFilter == NULL)
    {
        CommandError(COMMAND_ERROR_GENERIC, "Failed to allocate SectionFilter_t structure!");
        DeliveryMethodDestroy(dmInstance);
        ObjectRefDec(simplePIDFilter);
        PIDFilterFree(filter);
        return;
    }
    dvbpsiHandle = (dvbpsi_decoder_t*)malloc(sizeof(dvbpsi_decoder_t));
    sectionFilter->dvbpsiHandle = dvbpsiHandle;
    sectionFilter->dmInstance = dmInstance;
    dvbpsiHandle->pf_callback = SectionFilterGatherSections;
    dvbpsiHandle->p_private_decoder = sectionFilter;
    dvbpsiHandle->i_section_max_size = 1024;
    /* PSI decoder initial state */
    dvbpsiHandle->i_continuity_counter = 31;
    dvbpsiHandle->b_discontinuity = 1;
    dvbpsiHandle->p_current_section = NULL;
    dvbpsiHandle->p_free_sections = NULL;
    PIDFilterFilterPacketSet(filter, PIDFilterSimpleFilter, simplePIDFilter);
    PIDFilterProcessPacketSet(filter, SectionFilterProcessPacket, sectionFilter);

    filter->name = strdup(filterName);
    filter->type = SectionPIDFilterType;
    filter->enabled = TRUE;
}

static void CommandRemoveSecF(int argc, char **argv)
{
    PIDFilter_t *filter;
    PIDFilterSimpleFilter_t *simplePIDFilter;
    DeliveryMethodInstance_t *deliveryMethod;
    char *name;

    CommandCheckAuthenticated();

    FIND_SECTION_FILTER(argv[0]);

    name = filter->name;
    simplePIDFilter = filter->fpArg;
    deliveryMethod = filter->opArg;
    PIDFilterFree(filter);
    ObjectRefDec(simplePIDFilter);
    free(name);
    DeliveryMethodDestroy(deliveryMethod);
}

static void CommandListSecF(int argc, char **argv)
{
    TSFilter_t *tsFilter = MainTSFilterGet();
    ListIterator_t iterator;

    TSFilterLock(tsFilter);
    for ( ListIterator_Init(iterator, tsFilter->pidFilters);
          ListIterator_MoreEntries(iterator);
          ListIterator_Next(iterator))
    {
        PIDFilter_t *filter = (PIDFilter_t *)ListIterator_Current(iterator);
        if (strcmp(filter->type, SectionPIDFilterType) == 0)
        {
            CommandPrintf("%10s : %s\n", filter->name,  ((SectionFilter_t*)filter->ppArg)->dmInstance->mrl);
        }
    }
    TSFilterUnLock(tsFilter);
}

static void CommandSetOutputMRL(int argc, char **argv)
{
    PIDFilter_t *filter;
    DeliveryMethodInstance_t *instance;

    CommandCheckAuthenticated();

    FIND_SECTION_FILTER(argv[0]);

    pthread_mutex_lock(&sectionFiltersMutex);
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
    pthread_mutex_unlock(&sectionFiltersMutex);
}

/*******************************************************************************
* Processor Functions                                                          *
*******************************************************************************/
static TSPacket_t * SectionFilterProcessPacket(PIDFilter_t *pidfilter, void *arg, TSPacket_t *packet)
{
    SectionFilter_t *state = arg;
    dvbpsi_PushPacket(state->dvbpsiHandle, (uint8_t*)packet);
    return NULL;
}

static void SectionFilterGatherSections(dvbpsi_decoder_t* decoder, dvbpsi_psi_section_t* section)
{
    SectionFilter_t *state = decoder->p_private_decoder;
    DeliveryMethodInstance_t *dmInstance = state->dmInstance;
    dmInstance->ops->SendBlock(dmInstance, section->p_data, section->i_length + 3);
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


