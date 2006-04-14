#ifndef _UDPSEND_H
#define _UDPSEND_H
#include <netinet/in.h>
int UDPCreateSocket(void);
int UDPSetupSocketAddress(char *host, int port, struct sockaddr_in *sockaddr);
int UDPSendTo(int socket, char *data, int len, struct sockaddr_in *to);
#endif
