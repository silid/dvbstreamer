#ifndef _PATPROCESSOR_H
#define _PATPROCESSOR_H

void *PATProcessorCreate();
void PATProcessorDestroy(void *arg);
int PATProcessorProcessPacket(void *arg, TSPacket_t *packet);

#endif
