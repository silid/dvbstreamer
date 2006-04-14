#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "udpsend.h"

int main(int argc, char *argv[])
{
	FILE *fp;
	int socketfd;
	int port;
	struct sockaddr_in to;
	int packetcount = 0;
	int sleeptime = 0;
	int c = 0;
	printf("UDPSend test program\n");
	if (argc < 4)
	{
		printf("udpsend <file> <ip> <port>\n");
		exit(1);
	}
	
	fp = fopen(argv[1], "rb");
	if (NULL == fp)
	{
		printf("Failed to open %s\n", argv[1]);
		exit(2);
	}
	if (sscanf(argv[3], "%d", &port) == 0)
	{
		printf("Could not understand \"%s\"\n", argv[3]);
		exit(3);
	}
	if (argc > 4)
	{
		sscanf(argv[4], "%d", &packetcount);
		sscanf(argv[5], "%d", &sleeptime);
		
	}
	if (UDPSetupSocketAddress(argv[2], port, &to) == 0)
	{
		printf("Couldn't find \"%s\"\n", argv[2]);
		exit(4);
	}
	socketfd = UDPCreateSocket();
	while(!feof(fp))
	{
		char buffer[188];
		fread(buffer, sizeof(buffer), 1, fp);
		UDPSendTo(socketfd, buffer, sizeof(buffer), &to);
		if (packetcount)
		{
			if (c && ((c % packetcount) == 0))
			{
				usleep(sleeptime);
			}
		}
		c ++;
	}
	close(socketfd);
	fclose(fp);
}