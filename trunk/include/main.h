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

main.h

Entry point to the application.

*/
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
