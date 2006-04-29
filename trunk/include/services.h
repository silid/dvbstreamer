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
 
services.h
 
Manage services and PIDs.
 
*/
#ifndef _SERVICES_H
#define _SERVICES_H

#include "multiplexes.h"

#define MAX_SERVICES 400

typedef struct Service_t
{
    char *name;
    int multiplexfreq;
    int id;
    int pmtversion;
    int pmtpid;
}
Service_t;

typedef struct PID_t
{
    int pid;
    int type;
    int subtype;
    int pmtversion;
}
PID_t;

typedef void *ServiceEnumerator_t;

#define ServiceAreEqual(_service1, _service2) \
    (((_service1)->multiplexfreq == (_service2)->multiplexfreq) && \
    ((_service1)->id == (_service2)->id))

int ServiceCount();
int ServiceForMultiplexCount(int freq);

int ServiceAdd(int multiplexfreq, char *name, int id, int pmtversion, int pmtpid);
Service_t *ServiceGet(int i);
int ServicePMTVersionSet(Service_t  *service, int pmtversion);
int ServicePMTPIDSet(Service_t  *service, int pmtpid);

Service_t *ServiceFindName(char *name);
Service_t *ServiceFindId(Multiplex_t *multiplex, int id);

ServiceEnumerator_t ServiceEnumeratorGet();
ServiceEnumerator_t ServiceEnumeratorForMultiplex(int freq);
void ServiceEnumeratorDestroy(ServiceEnumerator_t enumerator);
Service_t *ServiceGetNext(ServiceEnumerator_t enumerator);

void ServiceFree(Service_t *service);


int ServicePIDAdd(Service_t *service, int pid, int type, int subtype, int pmtversion);
int ServicePIDRemove(Service_t *service);
int ServicePIDCount(Service_t *service);
int ServicePIDGet(Service_t *service, PID_t *pids, int *count);
#endif
