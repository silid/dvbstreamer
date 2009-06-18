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

nulloutput.c

NULL Delivery Method handler, doesn't write any output, to anywhere, period.

*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "plugin.h"
#include "ts.h"
#include "deliverymethod.h"

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
bool NullOutputCanHandle(char *mrl);
DeliveryMethodInstance_t *NullOutputCreate(char *arg);
void NullOutputSendPacket(DeliveryMethodInstance_t *this, TSPacket_t *packet);
void NullOutputSendBlock(DeliveryMethodInstance_t *this, void *block, unsigned long blockLen);
void NullOutputDestroy(DeliveryMethodInstance_t *this);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
/** Constants for the start of the MRL **/
#define PREFIX_LEN (sizeof(NullPrefix) - 1)
const char NullPrefix[] = "null://";

/** Plugin Interface **/
DeliveryMethodHandler_t NullOutputHandler = {
    NullOutputCanHandle,
    NullOutputCreate
};

static DeliveryMethodInstanceOps_t nullInstanceOps = {
    NullOutputSendPacket,
    NullOutputSendBlock,
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
* Plugin Setup                                                                 *
*******************************************************************************/
PLUGIN_FEATURES(
    PLUGIN_FEATURE_DELIVERYMETHOD(NullOutputHandler)
);

PLUGIN_INTERFACE_F(
    PLUGIN_FOR_ALL,
    "NullOutput", 
    "0.2", 
    "Null Delivery method, all packets are dropped.", 
    "charrea6@users.sourceforge.net"
);



/*******************************************************************************
* Delivery Method Functions                                                    *
*******************************************************************************/
bool NullOutputCanHandle(char *mrl)
{
    return (strncmp(NullPrefix, mrl, PREFIX_LEN) == 0);
}

DeliveryMethodInstance_t *NullOutputCreate(char *arg)
{
    return &singleInstance;
}

void NullOutputSendPacket(DeliveryMethodInstance_t *this, TSPacket_t *packet)
{
    /* Does nothing with the packet */
    return;
}

void NullOutputSendBlock(DeliveryMethodInstance_t *this, void *block, unsigned long blockLen)
{
    /* Does nothing with the packet */
    return;
}


void NullOutputDestroy(DeliveryMethodInstance_t *this)
{
    /* Does nothing */
    return;
}

