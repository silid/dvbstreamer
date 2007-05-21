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

psipprocessor.h

Process ATSC PSIP Tables.

*/
#ifndef _PSIPPROCESSOR_H
#define _PSIPPROCESSOR_H
#include "plugin.h"
#include "ts.h"

int PSIPProcessorInit(void);
void PSIPProcessorDeInit(void);

PIDFilter_t *PSIPProcessorCreate(TSFilter_t *tsfilter);
void PSIPProcessorDestroy(PIDFilter_t *filter);

void PSIPProcessorRegisterMGTCallback(PluginMGTProcessor_t callback);
void PSIPProcessorUnRegisterMGTCallback(PluginMGTProcessor_t callback);
void PSIPProcessorRegisterSTTCallback(PluginSTTProcessor_t callback);
void PSIPProcessorUnRegisterSTTCallback(PluginSTTProcessor_t callback);
void PSIPProcessorRegisterVCTCallback(PluginVCTProcessor_t callback);
void PSIPProcessorUnRegisterVCTCallback(PluginVCTProcessor_t callback);
#endif

