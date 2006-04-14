#ifndef _PMTPROCESSOR_H
#define _PMTPROCESSOR_H

void *PMTProcessorCreate();
void PMTProcessorDestroy(void *arg);
int PMTProcessorFilterPacket(void *arg, TSPacket_t *packet);
int PMTProcessorProcessPacket(void *arg, TSPacket_t *packet);
#endif
