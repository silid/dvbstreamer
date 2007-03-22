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

Outputs.c

Additional output management functions.

*/

#include <stdlib.h>
#include <string.h>

#include "ts.h"
#include "services.h"
#include "servicefilter.h"
#include "outputs.h"
#include "main.h"
#include "deliverymethod.h"
#include "list.h"

static void OutputFreeImpl(Output_t *output);

char *OutputErrorStr;

List_t *ManualOutputsList;
List_t *ServiceOutputsList;

int OutputsInit()
{
    ObjectRegisterType(Output_t);
    ManualOutputsList = ListCreate();
    ServiceOutputsList = ListCreate();
    return 0;
}

void OutputsDeInit()
{
    ListFree( ManualOutputsList, (ListDataDestructor_t)OutputFreeImpl);
    ListFree( ServiceOutputsList, (ListDataDestructor_t)OutputFreeImpl);
}

Output_t *OutputAllocate(char *name, OutputType type, char *destination)
{
    Output_t *output = NULL;
    ListIterator_t iterator;
    List_t *list = (type == OutputType_Manual) ? ManualOutputsList:ServiceOutputsList;
    for ( ListIterator_Init(iterator, list); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        output = ListIterator_Current(iterator);
        if (strcmp(name, output->name) == 0)
        {
            OutputErrorStr = "Output already exists!";
            return NULL;
        }
    }
    output =ObjectCreateType(Output_t);
    if (!output)
    {
        OutputErrorStr = "Not enough memory!";
        return NULL;
    }
    if (!ListAdd( list, output))
    {
        ObjectRefDec(output);
        OutputErrorStr = "Failed to add to list!";
        return NULL;
    }
    switch (type)
    {
        case OutputType_Manual:
            output->filter = PIDFilterAllocate(TSFilter);
            if (!output->filter)
            {
                OutputErrorStr = "Failed to allocate PID filter!";
                ListRemove( list, output);
                ObjectRefDec(output);
                return NULL;
            }
            output->filter->filterPacket = PIDFilterSimpleFilter;
            output->filter->fpArg = ObjectAlloc(sizeof(PIDFilterSimpleFilter_t));
            if (!output->filter->fpArg)
            {
                OutputErrorStr = "Failed to allocated PIDFilterSimpleFilter_t structure!";
                PIDFilterFree(output->filter);
                ListRemove( list, output);
                ObjectRefDec(output);
                return NULL;
            }
            break;
        case OutputType_Service:
            output->filter = ServiceFilterCreate(TSFilter, NULL, NULL);
            if (!output->filter)
            {
                OutputErrorStr = "Failed to allocate Service filter!";
                ListRemove( list, output);
                ObjectRefDec(output);
                return NULL;
            }
            break;
        default:
            OutputErrorStr = "Unknown output type!";
            return NULL;
    }
    output->type = type;

    if (!DeliveryMethodManagerFind(destination, output->filter))
    {
        OutputErrorStr = "Failed to find a delivery method!";
        switch (type)
        {
            case OutputType_Manual:
                ObjectFree(output->filter->fpArg);
                PIDFilterFree(output->filter);
                break;
            case OutputType_Service:
                ServiceFilterDestroy( output->filter);
                break;
        }
        ListRemove( list, output);
        ObjectRefDec(output);
        return NULL;
    }
    output->filter->enabled = 1;
    output->filter->name=output->name = strdup(name);

    return output;
}

void OutputFree(Output_t *output)
{
    List_t *list;
    switch (output->type)
    {
        case OutputType_Manual:
            list = ManualOutputsList;
            break;
        case OutputType_Service:
            list = ServiceOutputsList;
            break;
        default:
            OutputErrorStr = "Unknown output type!";
            return;
    }

    OutputFreeImpl(output);
    ListRemove( list, output);
}

static void OutputFreeImpl(Output_t *output)
{
    output->filter->enabled = 0;
    switch (output->type)
    {
        case OutputType_Manual:
            free(output->filter->fpArg);
            PIDFilterFree(output->filter);
            break;
        case OutputType_Service:
            ServiceFilterDestroy(output->filter);
            break;
        default:
            OutputErrorStr = "Unknown output type!";
            return;
    }
    free(output->name);

    DeliveryMethodManagerFree(output->filter);
    ObjectRefDec(output);
}
Output_t *OutputFind(char *name, OutputType type)
{
    Output_t *result = NULL;
    ListIterator_t iterator;
    List_t *list = (type == OutputType_Manual) ? ManualOutputsList:ServiceOutputsList;
    for ( ListIterator_Init(iterator, list); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        Output_t *output = ListIterator_Current(iterator);
        if (strcmp(output->name,name) == 0)
        {
            result = output;
            break;
        }
    }
    return result;
}

int OutputAddPID(Output_t *output, uint16_t pid)
{
    PIDFilterSimpleFilter_t *pids;
    if (output->type != OutputType_Manual)
    {
        OutputErrorStr = "Not a Manual Output!";
        return 1;
    }

    pids = (PIDFilterSimpleFilter_t *)output->filter->fpArg;
    if (pids->pidcount == MAX_PIDS)
    {
        OutputErrorStr = "No more available PID entries!";
        return 1;
    }
    pids->pids[pids->pidcount] = pid;
    pids->pidcount ++;

    return 0;
}

int OutputRemovePID(Output_t *output, uint16_t pid)
{
    int i;
    PIDFilterSimpleFilter_t *pids;
    if (output->type != OutputType_Manual)
    {
        OutputErrorStr = "Not a Manual Output!";
        return 1;
    }

    pids = (PIDFilterSimpleFilter_t *)output->filter->fpArg;
    for ( i = 0; i < pids->pidcount; i ++)
    {
        if (pids->pids[i] == pid)
        {
            memcpy(&pids->pids[i], &pids->pids[i + 1],
                   (pids->pidcount - (i + 1)) * sizeof(uint16_t));
            pids->pidcount --;
            return 0;
        }
    }
    OutputErrorStr = "PID not found!";
    return 1;
}

int OutputGetPIDs(Output_t *output, int *pidcount, uint16_t **pids)
{
    PIDFilterSimpleFilter_t *pidfilter;
    if (output->type != OutputType_Manual)
    {
        OutputErrorStr = "Not a Manual Output!";
        return 1;
    }

    pidfilter = (PIDFilterSimpleFilter_t *)output->filter->fpArg;
    *pidcount = pidfilter->pidcount;
    *pids = pidfilter->pids;
    return 0;
}

int OutputSetService(Output_t *output, Service_t *service)
{
    if (output->type != OutputType_Service)
    {
        OutputErrorStr = "Not a Service Output!";
        return 1;
    }

    ServiceFilterServiceSet(output->filter, service);
    return 0;
}

int OutputGetService(Output_t *output, Service_t **service)
{
    if (output->type != OutputType_Service)
    {
        OutputErrorStr = "Not a Service Output!";
        return 1;
    }

    *service = ServiceFilterServiceGet(output->filter);
    return 0;
}
