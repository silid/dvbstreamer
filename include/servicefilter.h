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
 
servicefilter.h
 
Filter all packets for a service include the PMT, rewriting the PAT sent out in
the output to only include this service.

*/
#ifndef _SERVICFILTER_H
#define _SERVICFILTER_H
#include "ts.h"

PIDFilter_t *ServiceFilterCreate(TSFilter_t *tsfilter, PacketOutput outputpacket,void *oparg);
void ServiceFilterDestroy(PIDFilter_t *filter);

void ServiceFilterServiceSet(PIDFilter_t *filter, Service_t *service);
Service_t *ServiceFilterServiceGet(PIDFilter_t *filter);

#endif
