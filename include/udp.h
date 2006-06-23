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
 
udp.h
 
Simplify UDP socket creation and packet sending.
 
*/
#ifndef _UDP_H
#define _UDP_H
#include <netinet/in.h>

#define MTU 1400 /* Conservative estimate */
#define IP_HEADER (5*4)
#define UDP_HEADER (2*4)

int UDPCreateSocket(sa_family_t family);

#define UDPSendTo(_socketfd, _data, _data_len, _to, _to_len) \
    sendto(_socketfd, _data, _data_len, 0, _to, _to_len)


#define UDPReceiveFrom(_socketfd, _data, _data_len, _from, _from_len) \
    recvfrom(_socketfd, _data, _data_len, 0, _from, _from_len)

#endif
