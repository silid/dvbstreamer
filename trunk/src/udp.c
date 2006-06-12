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
#include <sys/socket.h>
#include <string.h>
#include <netdb.h>
#include "udp.h"


int UDPCreateSocket(struct sockaddr_in *sockaddr, int reuse)
{
    int socketfd = socket(AF_INET,	SOCK_DGRAM,	IPPROTO_UDP);
    int reuseAddr = 1;

    if (socketfd < 0)
    {
        perror("socket()");
        return -1;
    }

    if (reuse)
    {
        if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &reuseAddr, sizeof(int)))
        {
            perror("setsockopt(SOL_SOCKET, SO_REUSEADDR)");
            close(socketfd);
            return -1;
        }
    }

    if (sockaddr)
    {
        if	(bind(socketfd, (struct sockaddr*)&sockaddr, sizeof(struct sockaddr_in))<0)
        {
            perror("bind()");
            close(socketfd);
            return	-1;
        }
    }

    return socketfd;
}

int UDPSetupSocketAddress(char *host, int port, struct sockaddr_in *sockaddr)
{
    struct hostent *hostinfo;
    sockaddr->sin_port=htons(port);
    hostinfo = gethostbyname(host);
    if (hostinfo != NULL)
    {
        sockaddr->sin_family = hostinfo->h_addrtype;
        memcpy((char *)&(sockaddr->sin_addr), hostinfo->h_addr, hostinfo->h_length);
        return 1;
    }
    return 0;
}

int UDPSendTo(int socketfd, char *data, int len, struct sockaddr_in *to)
{
    return sendto(socketfd, data, len, 0, (struct sockaddr*)to, sizeof(struct sockaddr_in));
}

int UDPReceiveFrom(int socketfd, char *data, int *len, struct sockaddr_in *from)
{
    int fromlen;
    int returnedLen = recvfrom(socketfd, data, *len, 0, (struct sockaddr*)from, &fromlen);
    if (returnedLen == -1)
    {
        *len = 0;
        return -1;
    }
    *len = returnedLen;
    return 0;
}
