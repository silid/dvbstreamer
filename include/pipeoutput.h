#ifndef _PIPEOUTPUT_H
#define _PIPEOUTPUT_H
void *PipeOutputCreate(char *arg);
void PipeOutputClose(void *arg);
void PipeOutputPacketOutput(void *arg, int numberOfPackets, TSPacket_t *packet);
#endif
