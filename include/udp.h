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

/**
 * @defgroup UDP UDP Socket functions
 * @{
 */

/**
 * Constant for the size of ethernet frame.
 * This is a conservative estimate.
 */
#define MTU 1400

/**
 * Constant for the size of the IP Header.
 */
#define IP_HEADER (5*4)

/**
 * Constant for the size of the UDP header.
 */
#define UDP_HEADER (2*4)

/**
 * Creates a UDP socket for the given family.
 * The socket family is intended to be either PF_INET or PF_INET6.
 * @param family Either PF_INET or PF_INET6.
 */
int UDPCreateSocket(sa_family_t family);

/**
 * Macro to simplify sending data to a socket.
 * @param _socketfd The socket file descriptor to send the data to.
 * @param _data Buffer containing the data to send.
 * @param _data_len Length of the data to send.
 * @param _to The buffer containing the address to send the data to.
 * @param _to_len Length of the _to buffer,
 */
#define UDPSendTo(_socketfd, _data, _data_len, _to, _to_len) \
    sendto(_socketfd, _data, _data_len, 0, _to, _to_len)

/**
 * Macro to simplify receiving data from a socket.
 * @param _socketfd The socket file descriptor to receive data from.
 * @param _data The buffer to receive the data in.
 * @param _data_len The length of the buffer.
 * @param _from Address buffer to receive the address of the sender.
 * @param _from_len The length of the _from buffer.
 */
#define UDPReceiveFrom(_socketfd, _data, _data_len, _from, _from_len) \
    recvfrom(_socketfd, _data, _data_len, 0, _from, _from_len)

/** @} */
#endif
