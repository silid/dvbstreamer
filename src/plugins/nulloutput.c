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
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>

#include "plugin.h"
#include "ts.h"
#include "deliverymethod.h"


bool NullOutputCanHandle(char *mrl);
DeliveryMethodInstance_t *NullOutputCreate(char *arg);
void NullOutputSendPacket(DeliveryMethodInstance_t *this, TSPacket_t *packet);
void NullOutputDestroy(DeliveryMethodInstance_t *this);

/** Plugin Interface **/
DeliveryMethodHandler_t NullOutputHandler = {
    NullOutputCanHandle,
    NullOutputCreate
};

PLUGIN_FEATURES(
    PLUGIN_FEATURE_DELIVERYMETHOD(NullOutputHandler)
);

PLUGIN_INTERFACE_F("Null Output", "0.1", "Null Delivery method, all packets are dropped.", "charrea6@users.sourceforge.net");

/** Constants for the start of the MRL **/
#define PREFIX_LEN (sizeof(NullPrefix) - 1)
char NullPrefix[] = "null://";

bool NullOutputCanHandle(char *mrl)
{
    return (strncmp(NullPrefix, mrl, PREFIX_LEN) == 0);
}

DeliveryMethodInstance_t *NullOutputCreate(char *arg)
{
    DeliveryMethodInstance_t *instance = calloc(1, sizeof(DeliveryMethodInstance_t));

    if (instance)
    {
        instance->SendPacket = NullOutputSendPacket;
        instance->DestroyInstance = NullOutputDestroy;
    }
    return instance;
}

void NullOutputSendPacket(DeliveryMethodInstance_t *this, TSPacket_t *packet)
{
    /* Does nothing with the packet */
    return;
}
void NullOutputDestroy(DeliveryMethodInstance_t *this)
{
    free(this);
}