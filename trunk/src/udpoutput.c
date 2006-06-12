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
 
UDP Output functions
 
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include "ts.h"
#include "udp.h"
#include "logging.h"

#define MAX_TS_PACKETS_PER_DATAGRAM ((MTU - (IP_HEADER+UDP_HEADER)) / sizeof(TSPacket_t))

struct UDPOutputState_t
{
    int socket;
    struct sockaddr_in address;
    int datagramfullcount;
    int tspacketcount;
    TSPacket_t outputbuffer[MAX_TS_PACKETS_PER_DATAGRAM];
};

void *UDPOutputCreate(char *arg)
{
    int port = 0;
    char *colon = NULL;
    char *host = "127.0.0.1";
    struct UDPOutputState_t *state = calloc(1, sizeof(struct UDPOutputState_t));
    if (state == NULL)
    {
        printlog(LOG_DEBUG, "Failed to allocate UDP Output state\n");
        return NULL;
    }
    state->socket = UDPCreateSocket(NULL, 0);
    if (state->socket == -1)
    {
        printlog(LOG_DEBUG, "Failed to create UDP socket\n");
        free(state);
        return NULL;
    }
    colon = strchr(arg, ':');
    if (colon == NULL)
    {
        port = atoi(arg);
        if (port == 0)
        {
            port = 9999;
        }
    }
    else
    {
        *colon = 0;
        port = atoi(colon + 1);
        if (strlen(arg) > 0)
        {
            host = arg;
        }
    }
    printlog(LOG_DEBUG,"UDP Host \"%s\" Port \"%d\"\n", host, port);
    if (UDPSetupSocketAddress(host, port, &state->address) == 0)
    {
        close(state->socket);
        free(state);
        return NULL;
    }
    if (colon)
    {
        *colon = ':';
    }
    state->datagramfullcount = MAX_TS_PACKETS_PER_DATAGRAM;
    return state;
}

void UDPOutputClose(void *arg)
{
    struct UDPOutputState_t *state = arg;
    close(state->socket);
    free(state);
}

void UDPOutputDatagramFullCountSet(void *udpoutput, int fullcount)
{
    struct UDPOutputState_t *state = udpoutput;
    if ((fullcount > 0) && (fullcount <= MAX_TS_PACKETS_PER_DATAGRAM))
    {
        state->datagramfullcount = fullcount;
    }
}

int UDPOutputDatagramFullCountGet(void *udpoutput)
{
    struct UDPOutputState_t *state = udpoutput;
    return state->datagramfullcount;
}

void UDPOutputPacketOutput(PIDFilter_t *pidfilter, void *arg, TSPacket_t *packet)
{
    struct UDPOutputState_t *state = arg;
    state->outputbuffer[state->tspacketcount++] = *packet;
    if (state->tspacketcount >= state->datagramfullcount)
    {
        UDPSendTo(state->socket, (char*)state->outputbuffer,
                  MAX_TS_PACKETS_PER_DATAGRAM * TSPACKET_SIZE, &state->address);
        state->tspacketcount = 0;
    }
}

char destinationBuffer[24]; /* 255.255.255.255:63536 */

char *UDPOutputDestination(void *arg)
{
    struct UDPOutputState_t *state = arg;
    unsigned int addr = ntohl(state->address.sin_addr.s_addr);
    sprintf(destinationBuffer, "%d.%d.%d.%d:%d",
            (addr & 0xff000000) >> 24,
            (addr & 0x00ff0000) >> 16,
            (addr & 0x0000ff00) >> 8,
            (addr & 0x000000ff) >> 0,
            ntohs(state->address.sin_port));
    return destinationBuffer;
}
