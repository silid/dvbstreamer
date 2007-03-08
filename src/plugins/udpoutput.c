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

udpoutput.c

UDP Output Delivery Method handler.

*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>

#include "plugin.h"
#include "ts.h"
#include "udp.h"
#include "deliverymethod.h"
#include "logging.h"

/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/
#define MTU 1400 /* Conservative estimate */
#define IP_HEADER (5*4)
#define UDP_HEADER (2*4)
#define MAX_TS_PACKETS_PER_DATAGRAM ((MTU - (IP_HEADER+UDP_HEADER)) / sizeof(TSPacket_t))

/* Default output targets if only host or port part is given */
#define DEFAULT_HOST "localhost"
#define DEFAULT_PORT "1234"


/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/
struct UDPOutputState_t
{
    char *mrl;
    void(*SendPacket)(DeliveryMethodInstance_t *this, TSPacket_t *packet);
    void(*SendBlock)(DeliveryMethodInstance_t *this, void *block, unsigned long blockLen);
    void(*DestroyInstance)(DeliveryMethodInstance_t *this);
    int socket;
    socklen_t addressLen;
    struct sockaddr_storage address;
    int datagramFullCount;
    int tsPacketCount;
    TSPacket_t outputBuffer[MAX_TS_PACKETS_PER_DATAGRAM];
};


/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
bool UDPOutputCanHandle(char *mrl);
DeliveryMethodInstance_t *UDPOutputCreate(char *arg);
void UDPOutputSendPacket(DeliveryMethodInstance_t *this, TSPacket_t *packet);
void UDPOutputSendBlock(DeliveryMethodInstance_t *this, void *block, unsigned long blockLen);
void UDPOutputDestroy(DeliveryMethodInstance_t *this);


/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
/** Constants for the start of the MRL **/
#define PREFIX_LEN (sizeof(UDPPrefix) - 1)
const char UDPPrefix[] = "udp://";

/** Plugin Interface **/
DeliveryMethodHandler_t UDPOutputHandler = {
            UDPOutputCanHandle,
            UDPOutputCreate
        };


/*******************************************************************************
* Plugin Setup                                                                 *
*******************************************************************************/

PLUGIN_FEATURES(
    PLUGIN_FEATURE_DELIVERYMETHOD(UDPOutputHandler)
);

PLUGIN_INTERFACE_F(
    "UDPOutput", 
    "0.1", 
    "Simple UDP Delivery method.\nUse udp://<host>:<port>", 
    "charrea6@users.sourceforge.net"
);


/*******************************************************************************
* Delivery Method Functions                                                    *
*******************************************************************************/
bool UDPOutputCanHandle(char *mrl)
{
    return (strncmp(UDPPrefix, mrl, PREFIX_LEN) == 0);
}

DeliveryMethodInstance_t *UDPOutputCreate(char *arg)
{
    char *host_start;
    int host_len;
    char hostbuffer[256];
    char *host = NULL;
    char *port = NULL;
    struct UDPOutputState_t *state;
#ifndef __CYGWIN__
    struct addrinfo *addrinfo, hints;

    /* Ignore the prefix */
    arg += PREFIX_LEN;

    if (arg[0] == '[')
    {
        host_start = arg + 1;
        port = strchr(arg, ']');
        if (port == NULL)
        {
            return NULL;
        }
        host_len = port - host_start;
        port++;
    }
    else
    {
        port = strchr(arg, ':');
        if (port == NULL)
        {
            host = strlen(arg) ? arg : DEFAULT_HOST;
        }
        else
        {
            host_start = arg;
            host_len = port - host_start;
        }
    }

    if (host == NULL)
    {
        if (host_len == 0)
        {
            host = DEFAULT_HOST;
        }
        else
        {
            if (host_len + 1 > sizeof(hostbuffer))
            {
                return NULL;
            }
            memcpy((void *)hostbuffer, host_start, host_len);
            hostbuffer[host_len] = 0;
            host = hostbuffer;
        }
    }

    if (port == NULL)
    {
        port = DEFAULT_PORT;
    }
    else
    {
        switch (*port)
        {
            case ':':
                port++;
                if (strlen(port))
                {
                    break;
                }
            /* fall though */
            case 0:
                port = DEFAULT_PORT;
                break;
            default:
                return NULL;
        }
    }
#endif
    state = calloc(1, sizeof(struct UDPOutputState_t));
    if (state == NULL)
    {
#ifndef __CYGWIN__
        printlog(LOG_DEBUG, "Failed to allocate UDP Output state\n");
#endif
        return NULL;
    }
    state->SendPacket = UDPOutputSendPacket;
    state->SendBlock = UDPOutputSendBlock;
    state->DestroyInstance = UDPOutputDestroy;
#ifndef __CYGWIN__
    printlog(LOG_DEBUG,"UDP Host \"%s\" Port \"%s\"\n", host, port);
    memset((void *)&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_ADDRCONFIG;
    if ((getaddrinfo(host, port, &hints, &addrinfo) != 0) || (addrinfo == NULL))
    {
        printlog(LOG_DEBUG, "Failed to set UDP target address\n");
        free(state);
        return NULL;
    }

    if (addrinfo->ai_addrlen > sizeof(struct sockaddr_storage))
    {
        freeaddrinfo(addrinfo);
        free(state);
        return NULL;
    }
    state->addressLen = addrinfo->ai_addrlen;
    memcpy(&state->address, addrinfo->ai_addr, addrinfo->ai_addrlen);
    freeaddrinfo(addrinfo);

    state->socket = UDPCreateSocket(state->address.ss_family);
    if (state->socket == -1)
    {
        printlog(LOG_DEBUG, "Failed to create UDP socket\n");
        free(state);
        return NULL;
    }
#endif
    state->datagramFullCount = MAX_TS_PACKETS_PER_DATAGRAM;
    return (DeliveryMethodInstance_t *)state;
}

void UDPOutputDestroy(DeliveryMethodInstance_t *this)
{
    struct UDPOutputState_t *state = (struct UDPOutputState_t *)this;
#ifndef __CYGWIN__
    close(state->socket);
#endif
    free(state);
}

void UDPOutputSendPacket(DeliveryMethodInstance_t *this, TSPacket_t *packet)
{
    struct UDPOutputState_t *state = (struct UDPOutputState_t*)this;
    state->outputBuffer[state->tsPacketCount++] = *packet;
    if (state->tsPacketCount >= state->datagramFullCount)
    {
        UDPSendTo(state->socket, (char*)state->outputBuffer,
                  MAX_TS_PACKETS_PER_DATAGRAM * TSPACKET_SIZE,
                  (struct sockaddr *)(&state->address), state->addressLen);
        state->tsPacketCount = 0;
    }
}

void UDPOutputSendBlock(DeliveryMethodInstance_t *this, void *block, unsigned long blockLen)
{
    struct UDPOutputState_t *state = (struct UDPOutputState_t*)this;
    UDPSendTo(state->socket, (char*)block,
              blockLen,
              (struct sockaddr *)(&state->address), state->addressLen);
}

