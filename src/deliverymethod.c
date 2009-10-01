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

deliverymethod.h

Delivery Method management functions.

*/
#include <stdlib.h>
#include <string.h>

#include "logging.h"
#include "list.h"
#include "deliverymethod.h"
/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/

bool NullOutputCanHandle(char *mrl);
DeliveryMethodInstance_t *NullOutputCreate(char *arg);
void NullOutputDestroy(DeliveryMethodInstance_t *this);
/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static char DELIVERYMETHOD[] = "DeliveryMethod";
static List_t *DeliveryMethodsList;
static List_t *InstancesList;

/** Constants for the start of the MRL **/
#define PREFIX_LEN (sizeof(NullPrefix) - 1)
const char NullPrefix[] = "null://";

/** Plugin Interface **/
DeliveryMethodHandler_t NullOutputHandler = {
    NullOutputCanHandle,
    NullOutputCreate
};

static DeliveryMethodInstanceOps_t nullInstanceOps = {
    NULL,
    NULL,
    NullOutputDestroy,
    NULL,
    NULL
};

static DeliveryMethodInstance_t singleInstance ={
       "null://",
        &nullInstanceOps,
        NULL
        };

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
    DeliveryMethodManagerRegister(&NullOutputHandler);
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

DeliveryMethodInstance_t *DeliveryMethodCreate(char *mrl)
{
    ListIterator_t iterator;
    DeliveryMethodInstance_t *instance = NULL;
    LogModule(LOG_DEBUG, DELIVERYMETHOD, "Looking for handler for %s", mrl);
    for ( ListIterator_Init(iterator, DeliveryMethodsList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        DeliveryMethodHandler_t *handler = ListIterator_Current(iterator);
        LogModule(LOG_DEBUG, DELIVERYMETHOD, "Checking handler %p", handler);
        if (handler->CanHandle(mrl))
        {
            instance = handler->CreateInstance(mrl);
            if (instance)
            {
                if (instance->mrl == NULL)
                {
                    LogModule(LOG_DEBUG, DELIVERYMETHOD, "MRL field not set when creating instance for %s", mrl);
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
    LogModule(LOG_DEBUG, DELIVERYMETHOD, "Released DeliveryMethodInstance(%p) for %s\n" ,instance, instance->mrl);
    instance->ops->DestroyInstance(instance);
    ListRemove(InstancesList, instance);
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

char* DeliveryMethodGetMRL(DeliveryMethodInstance_t *instance)
{
    return instance->mrl;
}

void DeliveryMethodReserveHeaderSpace(DeliveryMethodInstance_t *instance, int nrofPackets)
{
    if (instance->ops->ReserveHeaderSpace)
    {
        instance->ops->ReserveHeaderSpace(instance, nrofPackets);
    }
}

void DeliveryMethodSetHeader(DeliveryMethodInstance_t *instance, TSPacket_t *packets, int nrofPackets)
{
    if (instance->ops->SetHeader)
    {
        instance->ops->SetHeader(instance, packets, nrofPackets);
    }
}

void DeliveryMethodOutputPacket(DeliveryMethodInstance_t *instance, TSPacket_t* packet)
{
    if (instance->ops->OutputPacket)
    {
        instance->ops->OutputPacket(instance, packet);
    }
}

void DeliveryMethodOutputBlock(DeliveryMethodInstance_t *instance, void *block, unsigned long blockLen)
{
    if (instance->ops->OutputBlock)
    {
        instance->ops->OutputBlock(instance, block, blockLen);
    }
}

/*******************************************************************************
* NULL Delivery Method Functions                                                    *
*******************************************************************************/
bool NullOutputCanHandle(char *mrl)
{
    return (strncmp(NullPrefix, mrl, PREFIX_LEN) == 0);
}

DeliveryMethodInstance_t *NullOutputCreate(char *arg)
{
    return &singleInstance;
}

void NullOutputDestroy(DeliveryMethodInstance_t *this)
{
    /* Does nothing */
    return;
}


