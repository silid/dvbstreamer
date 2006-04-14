#ifndef _MAIN_H
#define _MAIN_H
#include "dvb.h"
#include "ts.h"
#include "services.h"
#include "multiplexes.h"

extern volatile Multiplex_t *CurrentMultiplex;
extern volatile Service_t *CurrentService;
extern volatile int CurrentMultiplexServiceChanges;

extern int verbosity;

extern void printlog(int level, char *format, ...);
extern Service_t *SetCurrentService(DVBAdapter_t *adapter, TSFilter_t *tsfilter, char *name);

#endif
