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

subtableprocessor.h

Generic Processor for PSI/SI tables that have several subtables on the same PID.

*/
#ifndef _SUBTABLEPROCESSOR_H
#define _SUBTABLEPROCESSOR_H
#include "plugin.h"
#include "ts.h"

PIDFilter_t *SubTableProcessorCreate(TSFilter_t *tsfilter, uint16_t pid, 
                                           dvbpsi_demux_new_cb_t subtablehandler, void *stharg, 
                                           MultiplexChanged multiplexchanged, void *mcarg);

void SubTableProcessorDestroy(PIDFilter_t *filter);

void SubTableProcessorRestart(PIDFilter_t *filter);
    
bool SubTableProcessorInit(PIDFilter_t *filter, uint16_t pid, 
                                   dvbpsi_demux_new_cb_t subtablehandler, void *stharg, 
                                   MultiplexChanged multiplexchanged, void *mcarg);

void SubTableProcessorDeinit(PIDFilter_t *filter);

void *SubTableProcessorGetSubTableHandlerArg(PIDFilter_t *filter);

#endif

