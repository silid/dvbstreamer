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

deliverymethod.h

Delivery Method management functions.

*/
#include <stdlib.h>
#include <string.h>

#include "logging.h"
#include "list.h"
#include "deliverymethod.h"


/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static char DELIVERYMETHOD[] = "DeliveryMethod";
static List_t *DeliveryMethodsList;
static List_t *InstancesList;

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
int DeliveryMethodManagerInit(void)
{
    DeliveryMethodsList = ListCreate();
    if (DeliveryMethodsList == NULL)
    {
        return -1;
    }
    InstancesList = ListCreate();
    if (InstancesList == NULL)
    {
        ListFree(DeliveryMethodsList, NULL);
        return -1;
    }
    return 0;
}

void DeliveryMethodManagerDeInit(void)
{
    ListFree(DeliveryMethodsList, NULL);
    if (ListCount(InstancesList) != 0)
    {
        LogModule(LOG_ERROR, DELIVERYMETHOD, "Instances still exist when shutting down Delivery Method Manager!\n");
    }
    ListFree(InstancesList, NULL);
}

void DeliveryMethodManagerRegister(DeliveryMethodHandler_t *handler)
{
    ListAdd(DeliveryMethodsList, handler);
}

void DeliveryMethodManagerUnRegister(DeliveryMethodHandler_t *handler)
{
    ListRemove(DeliveryMethodsList, handler);
}

bool DeliveryMethodManagerFind(char *mrl, PIDFilter_t *filter)
{
    DeliveryMethodInstance_t *instance = DeliveryMethodCreate(mrl);

    if (instance)
    {
        if (filter->enabled)
        {
            TSFilterLock(filter->tsFilter);
        }
        if (filter->outputPacket)
        {
            DeliveryMethodManagerFree(filter);
        }
        filter->outputPacket = DeliveryMethodOutputPacket;
        filter->opArg = instance;
        LogModule(LOG_DEBUG, DELIVERYMETHOD, "Created DeliveryMethodInstance(%p) for %s\n",instance, instance->mrl);
        if (filter->enabled)
        {
            TSFilterUnLock(filter->tsFilter);
        }
    }

    return instance != NULL;
}

void DeliveryMethodManagerFree(PIDFilter_t *filter)
{
    DeliveryMethodInstance_t *instance = filter->opArg;
    if (instance)
    {
        DeliveryMethodDestroy(instance);
        filter->opArg = NULL;
    }
}

DeliveryMethodInstance_t *DeliveryMethodCreate(char *mrl)
{
    ListIterator_t iterator;
    DeliveryMethodInstance_t *instance = NULL;
    
    for ( ListIterator_Init(iterator, DeliveryMethodsList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        DeliveryMethodHandler_t *handler = ListIterator_Current(iterator);
        if (handler->CanHandle(mrl))
        {
            instance = handler->CreateInstance(mrl);
            if (instance)
            {
                if (instance->mrl == NULL)
                {
                    instance->mrl = strdup(mrl);
                }
                ListAdd(InstancesList, instance);
                LogModule(LOG_DEBUG, DELIVERYMETHOD, "Created DeliveryMethodInstance(%p) for %s\n",instance, instance->mrl);
                break;
            }
        }
    }
    return instance;
}

void DeliveryMethodDestroy(DeliveryMethodInstance_t *instance)
{
    char *mrl = instance->mrl;
    instance->DestroyInstance(instance);
    ListRemove(InstancesList, instance);
    LogModule(LOG_DEBUG, DELIVERYMETHOD, "Released DeliveryMethodInstance(%p) for %s\n" ,instance, mrl);
    free(mrl);
}

void DeliveryMethodDestroyAll()
{
    ListIterator_t iterator;
    for (ListIterator_Init(iterator, InstancesList); 
         ListIterator_MoreEntries(iterator);)
     {
        DeliveryMethodInstance_t *instance = ListIterator_Current(iterator);
        ListIterator_Next(iterator);
        DeliveryMethodDestroy(instance);
     }
}

char* DeliveryMethodGetMRL(PIDFilter_t *filter)
{
    DeliveryMethodInstance_t *instance = filter->opArg;
    return instance->mrl;
}

void DeliveryMethodOutputPacket(PIDFilter_t *pidfilter, void *userarg, TSPacket_t* packet)
{
    DeliveryMethodInstance_t *instance = userarg;
    if (instance)
    {
        instance->SendPacket(instance, packet);
    }
}
