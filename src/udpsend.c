#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <string.h>
#include <netdb.h>
#include "udpsend.h"
#define PORT 54197 // 0xd3b5 ~= DVBS

int UDPCreateSocket(void)
{
    int socketfd = socket(AF_INET,	SOCK_DGRAM,	IPPROTO_UDP);
    int reuseAddr = 1;
    struct	sockaddr_in	address;
    if (socketfd < 0)
    {
        perror("socket()");
        return -1;
    }
    
    if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &reuseAddr, sizeof(int)))
    {
        perror("setsockopt(SOL_SOCKET, SO_REUSEADDR)");
        close(socketfd);
        return -1;
    }
    
    memset((void*)&address, 0, sizeof(address));
    address.sin_family=AF_INET;
    address.sin_addr.s_addr=INADDR_ANY;
    address.sin_port=htons(PORT);
    if	(bind(socketfd, (struct sockaddr*)&address, sizeof(address))<0)
    {
        perror("bind()");
		close(socketfd);
        return	-1;
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
