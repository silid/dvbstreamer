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
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>

#include "plugin.h"
#include "ts.h"
#include "udp.h"
#include "deliverymethod.h"
#include "logging.h"
#include "sap.h"

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

/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/
struct UDPOutputState_t
{
    /* !!! MUST BE THE FIRST FIELD IN THE STRUCTURE !!!
     * As the address of this field will be passed to all delivery method
     * functions and a 0 offset is assumed!
     */
    DeliveryMethodInstance_t instance;
    int socket;
    socklen_t addressLen;
    struct sockaddr_storage address;
    SAPSessionHandle_t sapHandle;
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
void UDPOutputInstall(bool installed);
static bool UDPOutputCanHandle(char *mrl);
static DeliveryMethodInstance_t *UDPOutputCreate(char *arg);
static void UDPOutputSendPacket(DeliveryMethodInstance_t *this, TSPacket_t *packet);
static void UDPOutputSendBlock(DeliveryMethodInstance_t *this, void *block, unsigned long blockLen);
static void UDPOutputDestroy(DeliveryMethodInstance_t *this);
static void RTPOutputSendPacket(DeliveryMethodInstance_t *this, TSPacket_t *packet);
static void RTPHeaderInit(uint8_t *header, uint16_t sequence);
static void CreateSAPSession(struct UDPOutputState_t *state, bool rtp, unsigned char ttl, char *sessionName);


/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
/** Constants for the start of the MRL **/
#define PREFIX_LEN (sizeof(UDPPrefix) - 1)
const char UDPPrefix[] = "udp://";
const char RTPPrefix[] = "rtp://";

DeliveryMethodInstanceOps_t UDPInstanceOps = {
    UDPOutputSendPacket,
    UDPOutputSendBlock,
    UDPOutputDestroy,
    NULL,
    NULL
};

DeliveryMethodInstanceOps_t RTPInstanceOps = {
    RTPOutputSendPacket,
    NULL,
    UDPOutputDestroy,
    NULL,
    NULL
};

const char UDPOUTPUT[] = "UDPOutput";

/*******************************************************************************
* Plugin Setup                                                                 *
*******************************************************************************/

PLUGIN_FEATURES(
    PLUGIN_FEATURE_DELIVERYMETHOD(UDPOutputCanHandle, UDPOutputCreate),
    PLUGIN_FEATURE_INSTALL(UDPOutputInstall)
);

PLUGIN_INTERFACE_F(
    PLUGIN_FOR_ALL,
    "UDPOutput",
    "0.3",
    "UDP Delivery methods.\n"
    "Use udp://[<host>:[<port>[:<ttl>[:session name]]]] for simple raw TS packets in a UDP datagram.\n"
    "Use rtp://[<host>:[<port>[:<ttl>[:session name]]]] for RTP encapsulation.\n"
    "Default host is localhost, default port is 1234",
    "charrea6@users.sourceforge.net"
);

void UDPOutputInstall(bool installed)
{
    if (installed)
    {
        SAPServerInit();
    }
    else
    {
        SAPServerDeinit();
    }
}

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
    char *sessionName= "DVBStreamer";
    bool rtp;
    char *mrl = arg;
    hostbuffer[0] = 0;
    portbuffer[0] = 0;

    /*
     * Process the mrl
     */

    /* Is this a RTP MRL? */
    rtp = (strncmp(RTPPrefix, mrl, PREFIX_LEN) == 0);

    /* Ignore the prefix */
    mrl += PREFIX_LEN;

    if (mrl[0] == '[')
    {
        mrl ++;
        LogModule(LOG_DEBUG, UDPOUTPUT, "IPv6 Address! %s\n", mrl);
        for (i = 0;mrl[i] && (mrl[i] != ']'); i ++)
        {
            hostbuffer[i] = mrl[i];
        }
        hostbuffer[i] = 0;
        mrl += i;
        if (*mrl == ']')
        {
            mrl ++;
        }
    }
    else
    {
        LogModule(LOG_DEBUG, UDPOUTPUT, "IPv4 Address! %s\n", mrl);
        for (i = 0;mrl[i] && (mrl[i] != ':'); i ++)
        {
            hostbuffer[i] = mrl[i];
        }
        hostbuffer[i] = 0;
        mrl += i;
    }
    /* Port */
    if (*mrl == ':')
    {
        mrl ++;
        LogModule(LOG_DEBUG, UDPOUTPUT, "Port parameter detected! %s\n", mrl);
        /* Process port */
        for (i = 0;mrl[i] && (mrl[i] != ':'); i ++)
        {
            portbuffer[i] = mrl[i];
        }
        portbuffer[i] = 0;
        mrl += i;
    }
    /* TTL */
    if (*mrl == ':')
    {
        char ttlbuffer[4];
        mrl ++;
        LogModule(LOG_DEBUG, UDPOUTPUT, "TTL parameter detected! %s\n", mrl);

        for (i = 0;mrl[i] && (mrl[i] != ':') && (i < 3); i ++)
        {
            ttlbuffer[i] = mrl[i];
        }
        /* process ttl */
        ttl = (unsigned char)atoi(ttlbuffer) & 255;
        mrl += i;
    }
    /* Anything else is the session name for SAP/SDP */
    if (*mrl == ':')
    {
        mrl ++;
        sessionName = mrl;
    }

    /*
     * Lookup the host name and port
     */
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
        state->instance.ops = &RTPInstanceOps;
    }
    else
    {
        state->instance.ops = &UDPInstanceOps;    
    }

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

    if (IsMulticastAddress(&state->address))
    {
        if (ttl > 1)
        {
            setsockopt(state->socket,IPPROTO_IP,IP_MULTICAST_TTL, &ttl,sizeof(ttl));
        }

       CreateSAPSession(state, rtp, ttl, sessionName);
    }

    state->datagramFullCount = MAX_TS_PACKETS_PER_DATAGRAM;
    state->instance.mrl = strdup(arg);
    return &state->instance;
}

static void UDPOutputDestroy(DeliveryMethodInstance_t *this)
{
    struct UDPOutputState_t *state = (struct UDPOutputState_t *)this;
    close(state->socket);
    if (state->sapHandle)
    {
        SAPServerDeleteSession(state->sapHandle);
    }
    free(this->mrl);
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
    header[4] = (temp >> 24) & 0xff;
    header[5] = (temp >> 16) & 0xff;
    header[6] = (temp >>  8) & 0xff;
    header[7] =  temp        & 0xff;

    /* SSRC (Not implemented) */
    header[8] = 0x0f;
    header[9] = 0x0f;
    header[10] = 0x0f;
    header[11] = 0x0f;
}

static void CreateSAPSession(struct UDPOutputState_t *state, bool rtp, unsigned char ttl, char *sessionName)
{
    char sdp[1000] = {0};
    char hostname[256];
    char addrtype[4];
    char ipaddr[256];
    char typevalue[256];
    in_port_t port = 0;
    struct timeval tv;

    gettimeofday(&tv,(struct timezone*) NULL);

    gethostname(hostname, sizeof(hostname) - 1);

#ifdef USE_GETADDRINFO
    {
        struct addrinfo *addrinfo, hints;
        memset((void *)&hints, 0, sizeof(hints));
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_flags = AI_ADDRCONFIG;
        if ((getaddrinfo(hostname, NULL, &hints, &addrinfo) != 0) || (addrinfo == NULL))
        {
            LogModule(LOG_DEBUG, UDPOUTPUT,"Failed to get host address\n");
            return;
        }

        if (addrinfo->ai_addrlen > sizeof(struct sockaddr_storage))
        {
            freeaddrinfo(addrinfo);
            return;
        }

        if (addrinfo->ai_family == AF_INET)
        {
            struct sockaddr_in *inaddr = (struct sockaddr_in *) addrinfo->ai_addr;
            inet_ntop(AF_INET, &inaddr->sin_addr, ipaddr, sizeof(ipaddr));
            strcpy(addrtype, "IP4");
        }
        else
        {
            struct sockaddr_in6 *in6addr = (struct sockaddr_in6 *) addrinfo->ai_addr;
            inet_ntop(AF_INET6, &in6addr->sin6_addr, ipaddr, sizeof(ipaddr));
            strcpy(addrtype, "IP6");
        }
        freeaddrinfo(addrinfo);
    }
#else
    {
        struct hostent *hostinfo;
        hostinfo = gethostbyname(hostname);
        if (hostinfo == NULL)
        {
            LogModule(LOG_DEBUG, UDPOUTPUT,"Failed to get host address\n");
            return;
        }
        inet_ntop(hostinfo->h_addrtype, hostinfo->h_addr, ipaddr, sizeof(ipaddr));
        if (hostinfo->h_addrtype)
        {
            strcpy(addrtype, "IP4");
        }
        else
        {
            strcpy(addrtype, "IP6");
        }
    }
#endif

#define SDPAdd(type,value) \
    do{\
        sprintf(typevalue,"%c=" value "\r\n", type);\
        strcat(sdp,typevalue);\
    }while(0)

#define SDPAddf(type,fmt,values...) \
    do{\
        sprintf(typevalue,"%c=" fmt "\r\n", type, values);\
        strcat(sdp,typevalue);\
    }while(0)

    SDPAdd('v',"0");
    SDPAddf('o',"- %ld%ld 0 IN %s %s", tv.tv_sec, tv.tv_usec, addrtype,  ipaddr);
    SDPAddf('s',"%s", sessionName);

    if (state->address.ss_family == AF_INET)
    {
        struct sockaddr_in *inaddr = (struct sockaddr_in *) &state->address;
        inet_ntop(AF_INET, &inaddr->sin_addr, ipaddr, sizeof(ipaddr));
        SDPAddf('c',"IN IP4 %s/%d", ipaddr,ttl);
        port = ntohs(inaddr->sin_port);
    }
#ifdef USE_GETADDRINFO
    else
    {
        struct sockaddr_in6 *in6addr = (struct sockaddr_in6 *) &state->address;
        inet_ntop(AF_INET6, &in6addr->sin6_addr, ipaddr, sizeof(ipaddr));
        SDPAddf('c',"IN IP6 %s", ipaddr);
        port = ntohs(in6addr->sin6_port);
    }
#endif
    SDPAdd('t',"0 0");

    if (rtp)
    {
        SDPAddf('m',"video %d RTP/AVP 33", port);
    }
    else
    {
        SDPAddf('m',"video %d udp 33", port);
    }

    state->sapHandle = SAPServerAddSession(&state->address, sdp);
}
