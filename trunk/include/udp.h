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
 
udpsend.h
 
Simplify UDP socket creation and packet sending.
 
*/
#ifndef _UDPSEND_H
#define _UDPSEND_H
#include <netinet/in.h>

#define MTU 1400 /* Conservative estimate */
#define IP_HEADER (5*4)
#define UDP_HEADER (2*4)

int UDPCreateSocket(struct sockaddr_in *sockaddr, int reuse);
int UDPSetupSocketAddress(char *host, int port, struct sockaddr_in *sockaddr);
int UDPSendTo(int socket, char *data, int len, struct sockaddr_in *to);
int UDPReceiveFrom(int socketfd, char *data, int *len, struct sockaddr_in *from);
#endif
