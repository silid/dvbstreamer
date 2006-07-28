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

static void DeliveryMethodOutputPacket(PIDFilter_t *pidfilter, void *userarg, TSPacket_t* packet);

static List_t *DeliveryMethodsList;
int DeliveryMethodManagerInit(void)
{
    DeliveryMethodsList = ListCreate();
    if (DeliveryMethodsList)
    {
        return 0;
    }
    return -1;
}

void DeliveryMethodManagerDeInit(void)
{
    ListFree(DeliveryMethodsList);
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
    ListIterator_t iterator;
    for ( ListIterator_Init(iterator, DeliveryMethodsList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        DeliveryMethodHandler_t *handler = ListIterator_Current(iterator);
        if (handler->CanHandle(mrl))
        {
            DeliveryMethodInstance_t *instance = handler->CreateInstance(mrl);
            if (instance)
            {
                filter->outputpacket = DeliveryMethodOutputPacket;
                filter->oparg = instance;
                if (instance->mrl == NULL)
                {
                    instance->mrl = strdup(mrl);
                }
                return TRUE;
            }
        }
    }
    return FALSE;
}

void DeliveryMethodManagerFree(PIDFilter_t *filter)
{
    DeliveryMethodInstance_t *instance = filter->oparg;
    instance->DestroyInstance(instance);
    filter->oparg = NULL;
}

char* DeliveryMethodGetMRL(PIDFilter_t *filter)
{
    DeliveryMethodInstance_t *instance = filter->oparg;
    return instance->mrl;
}

static void DeliveryMethodOutputPacket(PIDFilter_t *pidfilter, void *userarg, TSPacket_t* packet)
{
    DeliveryMethodInstance_t *instance = userarg;
    if (instance)
    {
        instance->SendPacket(instance, packet);
    }
}
