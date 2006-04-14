#ifndef _CACHE_H
#define _CACHE_H

#include "ts.h"
#include "multiplexes.h"
#include "services.h"


int CacheInit();
void CacheDeInit();

int CacheLoad(Multiplex_t *multiplex);
void CacheWriteback();


Service_t *CacheServiceFindId(int id);
Service_t *CacheServiceFindName(char *name, Multiplex_t **multiplex);

Service_t **CacheServicesGet(int *count);

PID_t *CachePIDsGet(Service_t *service, int *count);

void CacheUpdateMultiplex(Multiplex_t *multiplex, int patversion, int tsid);

void CacheUpdateService(Service_t *service, int pmtpid);

void CacheUpdatePIDs(Service_t *service, PID_t *pids, int count, int pmtversion);

#endif
