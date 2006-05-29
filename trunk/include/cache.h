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

cache.h

Caches service and PID information from the database for the current multiplex.

*/

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
void CacheUpdateServiceName(Service_t *service, char *name);

void CacheUpdatePIDs(Service_t *service, PID_t *pids, int count, int pmtversion);

Service_t *CacheServiceAdd(int id);
void CacheServiceDelete(Service_t *service);
#endif
