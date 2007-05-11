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
#include <sys/time.h>

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
#define RTP_HEADER_SIZE 12

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
    uint16_t sequence;
    /* rtpHeader must always come before outputBuffer as the order is important
       when sending RTP packets as rtpHeader is passed as the address of the 
       buffer to send!
    */
    uint8_t rtpHeader[RTP_HEADER_SIZE]; 
    TSPacket_t outputBuffer[MAX_TS_PACKETS_PER_DATAGRAM];
};

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static bool UDPOutputCanHandle(char *mrl);
static DeliveryMethodInstance_t *UDPOutputCreate(char *arg);
static void UDPOutputSendPacket(DeliveryMethodInstance_t *this, TSPacket_t *packet);
static void UDPOutputSendBlock(DeliveryMethodInstance_t *this, void *block, unsigned long blockLen);
static void UDPOutputDestroy(DeliveryMethodInstance_t *this);
static void RTPOutputSendPacket(DeliveryMethodInstance_t *this, TSPacket_t *packet);
static void RTPHeaderInit(uint8_t *header, uint16_t sequence);


/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
/** Constants for the start of the MRL **/
#define PREFIX_LEN (sizeof(UDPPrefix) - 1)
const char UDPPrefix[] = "udp://";
const char RTPPrefix[] = "rtp://";

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
    "0.2", 
    "UDP Delivery methods.\n"
    "Use udp://<host>:<port>[:<ttl>] for simple raw TS packets in a UDP datagram.\n"
    "Use rtp://<host>:<port>[:<ttl>] for RTP encapsulation.", 
    "charrea6@users.sourceforge.net"
);


/*******************************************************************************
* Delivery Method Functions                                                    *
*******************************************************************************/
static bool UDPOutputCanHandle(char *mrl)
{
    return (strncmp(UDPPrefix, mrl, PREFIX_LEN) == 0) || 
           (strncmp(RTPPrefix, mrl, PREFIX_LEN) == 0);
}

static DeliveryMethodInstance_t *UDPOutputCreate(char *arg)
{
    struct UDPOutputState_t *state;
    int i = 0;
    unsigned char ttl = 1;
    char hostbuffer[256];
    char portbuffer[6]; /* 65536\0 */
    bool rtp;

    hostbuffer[0] = 0;
    portbuffer[0] = 0;
    
    rtp = strncmp(RTPPrefix, arg, PREFIX_LEN) == 0;
    
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
    
    if (rtp)
    {
        state->SendPacket = RTPOutputSendPacket;
        state->SendBlock = NULL;        
    }
    else
    {
        state->SendPacket = UDPOutputSendPacket;
        state->SendBlock = UDPOutputSendBlock;
    }
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

static void UDPOutputDestroy(DeliveryMethodInstance_t *this)
{
    struct UDPOutputState_t *state = (struct UDPOutputState_t *)this;
    close(state->socket);
    free(state);
}

static void UDPOutputSendPacket(DeliveryMethodInstance_t *this, TSPacket_t *packet)
{
    struct UDPOutputState_t *state = (struct UDPOutputState_t*)this;
    state->outputBuffer[state->tsPacketCount++] = *packet;
    if (state->tsPacketCount >= state->datagramFullCount)
    {
        UDPSendTo(state->socket, (char*)state->outputBuffer,
                  state->datagramFullCount * TSPACKET_SIZE,
                  (struct sockaddr *)(&state->address), state->addressLen);
        state->tsPacketCount = 0;
    }
}

static void UDPOutputSendBlock(DeliveryMethodInstance_t *this, void *block, unsigned long blockLen)
{
    struct UDPOutputState_t *state = (struct UDPOutputState_t*)this;
    UDPSendTo(state->socket, (char*)block,
              blockLen,
              (struct sockaddr *)(&state->address), state->addressLen);
}

static void RTPOutputSendPacket(DeliveryMethodInstance_t *this, TSPacket_t *packet)
{
    struct UDPOutputState_t *state = (struct UDPOutputState_t*)this;
    state->outputBuffer[state->tsPacketCount++] = *packet;
    if (state->tsPacketCount >= state->datagramFullCount)
    {
        RTPHeaderInit(state->rtpHeader, state->sequence);
        UDPSendTo(state->socket, (char*)state->rtpHeader,
                  (state->datagramFullCount * TSPACKET_SIZE) + RTP_HEADER_SIZE,
                  (struct sockaddr *)(&state->address), state->addressLen);
        state->tsPacketCount = 0;
        state->sequence ++;
    }
}

static void RTPHeaderInit(uint8_t *header, uint16_t sequence)
{
    uint32_t temp;
    struct timeval tv;  
    gettimeofday(&tv,(struct timezone*) NULL);  
    /* Flags and payload type */
    header[0] = (2 << 6); /* Version 2, No Padding, No Extensions, No CSRC count */
    header[1] = 33;       /* No Marker, Payload type MP2T */

    /* Sequence */
    header[2] = (uint8_t) (sequence >> 8) & 0xff;
    header[3] = (uint8_t) (sequence >> 0) & 0xff;

    /* Time stamp */
    temp = ((tv.tv_sec%1000000)*1000000 + tv.tv_usec)/11; /* approximately a 90Khz clock (1000000/90000 = 11.1111111...)*/
    memcpy(&header[4], &temp, 4);

    /* SSRC (Not implemented) */
    header[8] = 0x0f;
    header[9] = 0x0f;
    header[10] = 0x0f;
    header[11] = 0x0f;
}
