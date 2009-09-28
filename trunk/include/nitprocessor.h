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

nitprocessor.h

Process Network Information Tables.

*/
#ifndef _NITPROCESSOR_H
#define _NITPROCESSOR_H
#include "plugin.h"
#include "ts.h"

typedef struct NITProcessor_s *NITProcessor_t;

NITProcessor_t NITProcessorCreate(TSReader_t *reader);
void NITProcessorDestroy(NITProcessor_t processor);

void NITProcessorRegisterNITCallback(PluginNITProcessor_t callback);
void NITProcessorUnRegisterNITCallback(PluginNITProcessor_t callback);

#endif
