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
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netdb.h>
#include "logging.h"
#include "udp.h"

#define PORT "54197" // 0xd3b5 ~= DVBS

int UDPCreateSocket(sa_family_t family)
{
    int socketfd = socket(family, SOCK_DGRAM, IPPROTO_UDP);
    int reuseAddr = 1;
#ifndef __CYGWIN__
    struct addrinfo *addrinfo, hints;
#endif
    int ret;

    if (socketfd < 0)
    {
        printlog(LOG_ERROR, "socket() failed\n");
        return -1;
    }

    if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &reuseAddr, sizeof(int)))
    {
        printlog(LOG_ERROR,"setsockopt(SOL_SOCKET, SO_REUSEADDR) failed\n");
        close(socketfd);
        return -1;
    }
#ifndef __CYGWIN__
    memset((void *)&hints, 0, sizeof(hints));
    hints.ai_family = family;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;
    ret = getaddrinfo(NULL, PORT, &hints, &addrinfo);
    if (ret != 0 || addrinfo == NULL)
    {
        printlog(LOG_ERROR, "getaddrinfo() failed with error %s\n", gai_strerror(ret));
        return -1;
    }

    if	(bind(socketfd, addrinfo->ai_addr, addrinfo->ai_addrlen)<0)
    {
        printlog(LOG_ERROR, "bind() failed\n");
        close(socketfd);
        socketfd = -1;
    }
    freeaddrinfo(addrinfo);
#endif
    return socketfd;
}
