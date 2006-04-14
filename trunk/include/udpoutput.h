#ifndef _UDPOUTPUT_H
#define _UDPOUTPUT_H
void *UDPOutputCreate(char *arg);
void UDPOutputClose(void *udpoutput);
void UDPOutputPacketOutput(void *arg, TSPacket_t *packet);
#endif
