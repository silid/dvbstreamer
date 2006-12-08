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

sectionprocessor.h

Section Processor Code.

*/
#ifndef _SECTIONFILTER_H
#define _SECTIONFILTER_H
/**
 * Start processing the specified PID and send sections to the specified callback.
 * @param pid The PID to process.
 * @param callback The function to call when a new section is received.
 */
void SectionProcessorStartPID(uint16_t pid, PluginSectionProcessor_t callback);

/**
 * Stop processing the specified PID for the specified callback.
 * Section from the PID will no longer be sent to the callback.
 * @param pid The PID to stop processing.
 * @param callback The function to remove from the list of callbacks.
 */
void SectionProcessorStopPID(uint16_t pid, PluginSectionProcessor_t callback);

/**
 * Destroy all Section Processors.
 * @internal
 */
void SectionProcessorDestroyAllProcessors(void);
#endif
