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

#ifdef __CYGWIN__
#define LogModule(_l, _m, _f...) fprintf(stderr, "%-15s : ", _m); fprintf(stderr, _f)
#endif

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

const char UDPOUTPUT[] = "UDPOutput";

/*******************************************************************************
* Plugin Setup                                                                 *
*******************************************************************************/

PLUGIN_FEATURES(
    PLUGIN_FEATURE_DELIVERYMETHOD(UDPOutputHandler)
);

PLUGIN_INTERFACE_F(
    PLUGIN_FOR_ALL,
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
    struct UDPOutputState_t *state;
    int i = 0;
    unsigned char ttl = 1;
    char hostbuffer[256];
    char portbuffer[6]; /* 65536\0 */

    /* Ignore the prefix */
    arg += PREFIX_LEN;

    if (arg[0] == '[')
    {
        arg ++;
        LogModule(LOG_DEBUG, UDPOUTPUT, "IPv6 Address! %s\n", arg);
        for (i = 0;arg[i] && (arg[i] != ']'); i ++)
        {
            hostbuffer[i] = arg[i];
        }
        hostbuffer[i] = 0;
        arg += i;
        if (*arg == ']')
        {
            arg ++;
        }
    }
    else
    {
        LogModule(LOG_DEBUG, UDPOUTPUT, "IPv4 Address! %s\n", arg);
        for (i = 0;arg[i] && (arg[i] != ':'); i ++)
        {
            hostbuffer[i] = arg[i];
        }
        hostbuffer[i] = 0;
        arg += i;
    }

    if (*arg == ':')
    {
        arg ++;
        LogModule(LOG_DEBUG, UDPOUTPUT, "Port parameter detected! %s\n", arg);
        /* Process port */
        for (i = 0;arg[i] && (arg[i] != ':'); i ++)
        {
            portbuffer[i] = arg[i];
        }
        portbuffer[i] = 0;
        arg += i;
        if (*arg == ':')
        {
            arg ++;
            LogModule(LOG_DEBUG, UDPOUTPUT, "TTL parameter detected! %s\n", arg);
            /* process ttl */
            ttl = (unsigned char)atoi(arg) & 255;
        }
    }
    if (hostbuffer[0] == 0)
    {
        strcpy(hostbuffer, DEFAULT_HOST);
    }
    if (portbuffer[0] == 0)
    {
        strcpy(portbuffer, DEFAULT_PORT);
    }
    state = calloc(1, sizeof(struct UDPOutputState_t));
    if (state == NULL)
    {
        LogModule(LOG_DEBUG, UDPOUTPUT, "Failed to allocate UDP Output state\n");
        return NULL;
    }
    state->SendPacket = UDPOutputSendPacket;
    state->SendBlock = UDPOutputSendBlock;
    state->DestroyInstance = UDPOutputDestroy;

    LogModule(LOG_DEBUG, UDPOUTPUT, "UDP Host \"%s\" Port \"%s\" TTL %d\n", hostbuffer, portbuffer, ttl);
#ifdef USE_GETADDRINFO    
    {
        struct addrinfo *addrinfo, hints;
        memset((void *)&hints, 0, sizeof(hints));
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_flags = AI_ADDRCONFIG;
        if ((getaddrinfo(hostbuffer, portbuffer, &hints, &addrinfo) != 0) || (addrinfo == NULL))
        {
            LogModule(LOG_DEBUG, UDPOUTPUT,"Failed to set UDP target address\n");
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
    }
#else
    {
        struct sockaddr_in sockaddr;
        struct hostent *hostinfo;
        sockaddr.sin_port=htons(atoi(portbuffer));
        hostinfo = gethostbyname(hostbuffer);
        if (hostinfo == NULL)
        {
            LogModule(LOG_DEBUG, UDPOUTPUT,"Failed to set UDP target address\n");
            return NULL;
        }
        sockaddr.sin_family = hostinfo->h_addrtype;
        memcpy((char *)&(sockaddr.sin_addr), hostinfo->h_addr, hostinfo->h_length);
        
        memcpy(&state->address, &sockaddr, sizeof(sockaddr));
        state->addressLen = sizeof(sockaddr);
    }
#endif

    state->socket = UDPCreateSocket(state->address.ss_family);
    if (state->socket == -1)
    {
        LogModule(LOG_DEBUG, UDPOUTPUT,"Failed to create UDP socket\n");
        free(state);
        return NULL;
    }
    
    if (ttl > 1)
    {
        setsockopt(state->socket,IPPROTO_IP,IP_MULTICAST_TTL, &ttl,sizeof(ttl));
    }
    
    state->datagramFullCount = MAX_TS_PACKETS_PER_DATAGRAM;
    return (DeliveryMethodInstance_t *)state;
}

void UDPOutputDestroy(DeliveryMethodInstance_t *this)
{
    struct UDPOutputState_t *state = (struct UDPOutputState_t *)this;
    close(state->socket);
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

