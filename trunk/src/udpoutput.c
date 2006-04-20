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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ts.h"
#include "udpsend.h"
#include "logging.h"

#define MTU 1400 /* Conservative estimate */
#define IP_HEADER (5*4)
#define UDP_HEADER (2*4)
#define MAX_TS_PACKETS_PER_DATAGRAM ((MTU - (IP_HEADER+UDP_HEADER)) / sizeof(TSPacket_t))

struct UDPOutputState_t
{
	int socket;
	struct sockaddr_in address;
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
	state->socket = UDPCreateSocket();
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
		*colon = ';';
	}

	return state;
}

void UDPOutputClose(void *arg)
{
	struct UDPOutputState_t *state = arg;
	close(state->socket);
	free(state);
}

void UDPOutputPacketOutput(void *arg, TSPacket_t *packet)
{
	struct UDPOutputState_t *state = arg;
	state->outputbuffer[state->tspacketcount++] = *packet;
	if (state->tspacketcount >= MAX_TS_PACKETS_PER_DATAGRAM)
	{
		UDPSendTo(state->socket, (char*)state->outputbuffer,
			MAX_TS_PACKETS_PER_DATAGRAM * TSPACKET_SIZE, &state->address);
		state->tspacketcount = 0;
	}
}
