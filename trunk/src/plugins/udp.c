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

udpsend.c

Simplify UDP socket creation and packet sending.

*/
#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netdb.h>
#include "main.h"
#include "logging.h"
#include "udp.h"

/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/

#define PORT 54197 // 0xd3b5 ~= DVBS

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static const char UDP[] = "UDP";

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
int UDPCreateSocket(sa_family_t family)
{
    int socketfd = socket(family, SOCK_DGRAM, IPPROTO_UDP);
    int reuseAddr = 1;
    struct sockaddr *addr;
    socklen_t addr_len;
#ifdef USE_GETADDRINFO
    struct addrinfo *addrinfo, hints;
    int ret;
#else
    struct sockaddr_in ip4addr;
#endif

    if (socketfd < 0)
    {
        LogModule(LOG_ERROR, UDP, "socket() failed\n");
        return -1;
    }

    if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &reuseAddr, sizeof(int)))
    {
        LogModule(LOG_ERROR, UDP, "setsockopt(SOL_SOCKET, SO_REUSEADDR) failed\n");
        close(socketfd);
        return -1;
    }
#ifdef USE_GETADDRINFO
    memset((void *)&hints, 0, sizeof(hints));
    hints.ai_family = family;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;
    ret = getaddrinfo(NULL, TOSTRING(PORT), &hints, &addrinfo);
    if (ret != 0 || addrinfo == NULL)
    {
        LogModule(LOG_ERROR, UDP, "getaddrinfo() failed with error %s\n", gai_strerror(ret));
        return -1;
    }
    addr = addrinfo->ai_addr;
    addr_len = addrinfo->ai_addrlen;
#else
    ip4addr.sin_family = AF_INET;
    ip4addr.sin_port = PORT;
    ip4addr.sin_addr.s_addr = INADDR_ANY;
    addr = (struct sockaddr*)&ip4addr;
    addr_len = sizeof(ip4addr);
#endif

    if (bind(socketfd, addr, addr_len)<0)
    {
        LogModule(LOG_ERROR,UDP, "bind() failed\n");
        close(socketfd);
        socketfd = -1;
    }

#ifdef USE_GETADDRINFO
    freeaddrinfo(addrinfo);
#endif
    return socketfd;
}
