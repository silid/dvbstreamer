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
#include "udpoutput.h"
#include "outputs.h"
#include "main.h"


char *OutputErrorStr;
Output_t Outputs[MAX_OUTPUTS];

int OutputsInit()
{
    /* Clear all outputs */
    memset(&Outputs, 0, sizeof(Outputs));
    return 0;
}

void OutputsDeInit()
{
    int i;
    for (i = 0; i < MAX_OUTPUTS; i ++)
    {
        if (!Outputs[i].name)
        {
            continue;
        }
        OutputFree(&Outputs[i]);
    }
}

Output_t *OutputAllocate(char *name, OutputType type, char *destination)
{
    Output_t *output = NULL;
    int i;
    
    for (i = 0; i < MAX_OUTPUTS; i ++)
    {
        if (Outputs[i].name && (strcmp(name, Outputs[i].name) == 0) &&
            (type == Outputs[i].type))
        {
            OutputErrorStr = "Output already exists!";
            return NULL;
        }
        if ((output == NULL ) && (Outputs[i].name == NULL))
        {
            output = &Outputs[i];
        }
    }
    if (!output)
    {
        OutputErrorStr = "No free output slots!";
        return NULL;
    }
    switch (type)
    {
        case OutputType_Manual:
            output->filter = PIDFilterAllocate(TSFilter);
            if (!output->filter)
            {
                OutputErrorStr = "Failed to allocate PID filter!";
                return NULL;
            }
            output->filter->filterpacket = PIDFilterSimpleFilter;
            output->filter->fparg = calloc(1, sizeof(PIDFilterSimpleFilter_t));
            if (!output->filter->fparg)
            {
                OutputErrorStr = "Failed to allocated PIDFilterSimpleFilter_t structure!";
                PIDFilterFree(output->filter);
                return NULL;
            }
            break;
        case OutputType_Service:
            output->filter = ServiceFilterCreate(TSFilter, NULL, NULL);
            if (!output->filter)
            {
                OutputErrorStr = "Failed to allocate Service filter!";
                return NULL;
            }
            break;
        default:
            OutputErrorStr = "Unknown output type!";
            return NULL;
    }
    output->type = type;
    output->filter->outputpacket = UDPOutputPacketOutput;
    output->filter->oparg = UDPOutputCreate(destination);
    if (!output->filter->oparg)
    {
        OutputErrorStr = "Failed to parse ip and udp port!";
        free(output->filter->fparg);
        PIDFilterFree(output->filter);
        return NULL;
    }
    output->filter->enabled = 1;
    output->name = strdup(name);

    return output;
}

void OutputFree(Output_t *output)
{
    output->filter->enabled = 0;
    switch (output->type)
    {
        case OutputType_Manual:
            free(output->filter->fparg);
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
    
    UDPOutputClose(output->filter->oparg);
    memset(output, 0, sizeof(Output_t));
}

Output_t *OutputFind(char *name, OutputType type)
{
    Output_t *output = NULL;
    int i;
    for (i = 0; i < MAX_OUTPUTS; i ++)
    {
        if (Outputs[i].name && 
            (strcmp(Outputs[i].name,name) == 0) && (Outputs[i].type == type))
        {
            output = &Outputs[i];
            break;
        }
    }
    return output;
}

int OutputAddPID(Output_t *output, uint16_t pid)
{
    PIDFilterSimpleFilter_t *pids;
    if (output->type != OutputType_Manual)
    {
        OutputErrorStr = "Not a Manual Output!";
        return 1;
    }
    
    pids = (PIDFilterSimpleFilter_t *)output->filter->fparg;
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
    
    pids = (PIDFilterSimpleFilter_t *)output->filter->fparg;
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
    
    pidfilter = (PIDFilterSimpleFilter_t *)output->filter->fparg;
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
