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

sap.c

Session Announcement Protocol.

*/

#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <netdb.h>
#include <pthread.h>

#include "main.h"
#include "dispatchers.h"
#include "logging.h"
#include "list.h"
#include "objects.h"
#include "udp.h"
#include "sap.h"
/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/
#define SAP_PORT (9875)

/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/

typedef struct SAPSession_s {
    bool deleted;
    uint16_t messageIdHash;
    struct sockaddr_storage originatingSource;
    char sdp[1000];
}SAPSession_t;

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void SAPSessionFree(void *data);
static int CreateSAPPacket(SAPSession_t *session, uint8_t *packet);
static void DetermineSAPMulticast(SAPSession_t *session, struct sockaddr_storage *sockAddr);
static void *SAPServer(void *arg);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static const char mimeType[] = "application/sdp";
static List_t *sessionList;

static pthread_cond_t messageDelayCond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t sessionListMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t serverThread;

static uint16_t nextMessageIdHash = 1;

static bool quit = FALSE;

static char SAP[] = "SAP";

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/

bool IsMulticastAddress(struct sockaddr_storage *addr)
{
    bool result = FALSE;
    if (addr->ss_family == PF_INET)
    {
        struct sockaddr_in *inAddr = (struct sockaddr_in *)addr;
        in_addr_t ip = ntohl(inAddr->sin_addr.s_addr);
        result = ((ip >= 0xe0000000) && (ip <= 0xefffffff));
        LogModule(LOG_DEBUG, SAP, "ip=0x%08x result=%d\n", ip,result);
    }
    else
    {
        struct sockaddr_in6 *in6Addr = (struct sockaddr_in6*)addr;
        result = (in6Addr->sin6_addr.s6_addr[0] == 0xff);
    }
    return result;
}

void SAPServerInit(void)
{
    ObjectRegisterType(SAPSession_t);
    sessionList = ListCreate();
    pthread_create(&serverThread, NULL, SAPServer, NULL);
}

void SAPServerDeinit(void)
{
    quit = TRUE;
    pthread_cond_signal(&messageDelayCond);
    pthread_join(serverThread, NULL);
    LogUnregisterThread(serverThread);
    ListFree(sessionList, SAPSessionFree);
}

SAPSessionHandle_t SAPServerAddSession(struct sockaddr_storage *originatingSource, char *sdp)
{
    SAPSession_t *session;
    pthread_mutex_lock(&sessionListMutex);
    session = ObjectCreateType(SAPSession_t);
    if (session)
    {
        session->messageIdHash = nextMessageIdHash;
        nextMessageIdHash ++;
        memcpy(&session->originatingSource, originatingSource, sizeof(struct sockaddr_storage));
        strcpy(session->sdp, sdp);

        ListAdd(sessionList, session);
    }
    pthread_mutex_unlock(&sessionListMutex);
    LogModule(LOG_DEBUG, SAP, "Added SAP session %x sdp:\n%s", session,sdp);
    return session;
}

void SAPServerDeleteSession(SAPSessionHandle_t handle)
{
    SAPSession_t *session = handle;
    ListIterator_t iterator;
    pthread_mutex_lock(&sessionListMutex);
    ListRemove(sessionList, session);
    session->deleted = TRUE;
    ListIterator_Init(iterator, sessionList);
    ListInsertBeforeCurrent(&iterator, session);
    pthread_mutex_unlock(&sessionListMutex);
    LogModule(LOG_DEBUG, SAP, "Deleted SAP session %x\n", handle);
}
/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/
static void SAPSessionFree(void *data)
{
    ObjectRefDec(data);
}

static int CreateSAPPacket(SAPSession_t *session, uint8_t *packet)
{
    int mimeTypeOffset = 0;
    /* Byte 0 : Version number V1        = 001      (3 bits)
     *          Address type   IPv4/IPv6 = 0/1      (1 bit)
     *          Reserved                   0        (1 bit)
     *          Message Type   ann/del   = 0/1      (1 bit)
     *          Encryption     on/off    = 0/1      (1 bit)
     *          Compressed     on/off    = 0/1      (1 bit) */
    packet[0] = 0x20 | (session->deleted ? 4:0);
    packet[1] = 0x00; /* Authentication length (not supported) */
    packet[2] = session->messageIdHash & 0xff;
    packet[3] = (session->messageIdHash >> 8) & 0xff;

    if (session->originatingSource.ss_family == PF_INET)
    {
        struct sockaddr_in *inAddr =(struct sockaddr_in *)&session->originatingSource;
        mimeTypeOffset = 8;
        memcpy(&packet[4], &inAddr->sin_addr, 4);
    }
    else
    {
        struct sockaddr_in6 *in6Addr =(struct sockaddr_in6 *)&session->originatingSource;
        mimeTypeOffset = 20;
        memcpy(&packet[4], &in6Addr->sin6_addr, 16);
    }
    memcpy(&packet[mimeTypeOffset], mimeType, sizeof(mimeType));
    memcpy(&packet[mimeTypeOffset + sizeof(mimeType)], session->sdp, strlen(session->sdp));
    return mimeTypeOffset + sizeof(mimeType) + strlen(session->sdp);
}

static void DetermineSAPMulticast(SAPSession_t *session, struct sockaddr_storage *sockAddr)
{

    if (session->originatingSource.ss_family == PF_INET)
    {
        struct sockaddr_in *sessionAddr4 = (struct sockaddr_in*)&session->originatingSource;
        struct sockaddr_in *sockAddr4 = (struct sockaddr_in *)sockAddr;
        /* str is an IPv4 address */
        in_addr_t ip = ntohl (sessionAddr4->sin_addr.s_addr);

        /* 224.0.0.0/24 => 224.0.0.255 */
        if ((ip & 0xffffff00) == 0xe0000000)
        {
            ip =  0xe00000ff;
        }
        /* 239.255.0.0/16 => 239.255.255.255 */
        else if ((ip & 0xffff0000) == 0xefff0000)
        {
            ip =  0xefffffff;
        }
        /* 239.192.0.0/14 => 239.195.255.255 */
        else if ((ip & 0xfffc0000) == 0xefc00000)
        {
            ip =  0xefc3ffff;
        }
        /* other multicast address => 224.2.127.254 */
        else
        {
            ip = 0xe0027ffe;
        }

        ip = htonl (ip);

        memset (sockAddr4, 0, sizeof (struct sockaddr_in));
        sockAddr4->sin_family = AF_INET;
        sockAddr4->sin_port = htons (SAP_PORT);
        memcpy (&sockAddr4->sin_addr.s_addr, &ip, sizeof (ip));
    }
    else
    {
        struct sockaddr_in6 *sessionAddr6 = (struct sockaddr_in6*)&session->originatingSource;
        struct sockaddr_in6 *sockAddr6 = (struct sockaddr_in6 *)sockAddr;
        memset (sockAddr6, 0, sizeof (struct sockaddr_in6));
        sockAddr6->sin6_family = AF_INET6;
        sockAddr6->sin6_scope_id = sessionAddr6->sin6_scope_id;
        sockAddr6->sin6_port = htons (SAP_PORT);
        memcpy (&sockAddr6->sin6_addr.s6_addr,
                "\xff\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x7f\xfe",
                16);
        sockAddr6->sin6_addr.s6_addr[1] = sessionAddr6->sin6_addr.s6_addr[1] & 0x0f;
    }
}

static void *SAPServer(void *arg)
{
    struct timespec waitFor;
    uint8_t packet[UDP_PAYLOAD_SIZE];
    int packetLen;
    SAPSession_t *session;
    int sessionCount;
    ListIterator_t iterator;
    struct sockaddr_storage sapMulticastAddr;

    int socket4 = UDPCreateSocket(PF_INET);
    int socket6 = UDPCreateSocket(PF_INET6);
    int ttl = 255;

    LogRegisterThread(serverThread, "SAP");

    if (socket4 != -1)
    {
        setsockopt(socket4,IPPROTO_IP,IP_MULTICAST_TTL, &ttl,sizeof(ttl));
    }
    if (socket6 != -1)
    {
        setsockopt(socket6,IPPROTO_IPV6,IPV6_MULTICAST_HOPS, &ttl,sizeof(ttl));
    }
    LogModule(LOG_DEBUG, SAP, "Annoucement thread starting\n");
    while (!quit)
    {
        pthread_mutex_lock(&sessionListMutex);
        clock_gettime(CLOCK_REALTIME, &waitFor);
        sessionCount = ListCount(sessionList);
        if (sessionCount > 0)
        {
            /* Remove the first message from the front of the list, send it and
             * add it to the back.
             */
            ListIterator_Init(iterator, sessionList);
            session = (SAPSession_t*)ListIterator_Current(iterator);
            ListRemoveCurrent(&iterator);
            if (session->deleted)
            {
                ObjectRefDec(session);
            }
            else
            {
                ListAdd(sessionList, session);
            }

            packetLen = CreateSAPPacket(session, packet);
            DetermineSAPMulticast(session, &sapMulticastAddr);
            /* Send the message */
            if (session->originatingSource.ss_family == PF_INET)
            {
                if (socket4 != -1)
                {
                    UDPSendTo(socket4, packet, packetLen, (struct sockaddr *)&sapMulticastAddr, sizeof(struct sockaddr_in));
                }
            }
            else
            {
                if (socket6 != -1)
                {
                    UDPSendTo(socket6, packet, packetLen, (struct sockaddr *)&sapMulticastAddr, sizeof(struct sockaddr_in6));
                }
            }
        }
        waitFor.tv_sec += 1;
        pthread_cond_timedwait(&messageDelayCond, &sessionListMutex, &waitFor);
        pthread_mutex_unlock(&sessionListMutex);

    }
    LogModule(LOG_DEBUG, SAP, "Annoucement thread finished.\n");
    if (socket4 != -1)
    {
        close(socket4);
    }
    if (socket6 != -1)
    {
        close(socket6);
    }
    return NULL;
}
