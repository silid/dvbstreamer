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

sdtprocessor.h

Process Service Description Tables and update the services information.

*/
#ifndef _SDTPROCESSOR_H
#define _SDTPROCESSOR_H
#include "plugin.h"
#include "ts.h"

PIDFilter_t *SDTProcessorCreate(TSFilter_t *tsfilter);
void SDTProcessorDestroy(PIDFilter_t *filter);

void SDTProcessorRegisterSDTCallback(PluginSDTProcessor_t callback);
void SDTProcessorUnRegisterSDTCallback(PluginSDTProcessor_t callback);

#endif
