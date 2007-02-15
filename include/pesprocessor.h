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

pesprocessor.h

PES Processor Code.

*/
#ifndef _PESFILTER_H
#define _PESFILTER_H
/**
 * Start processing the specified PID and send PES sections to the specified callback.
 * @param pid The PID to process.
 * @param callback The function to call when a new section is received.
 * @param userarg User argument to pass to the callback when a section arrives.
 */
void PESProcessorStartPID(uint16_t pid, PluginPESProcessor_t callback, void *userarg);

/**
 * Stop processing the specified PID for the specified callback.
 * PES sections from the PID will no longer be sent to the callback.
 * @param pid The PID to stop processing.
 * @param callback The function to remove from the list of callbacks.
 * @param userarg User argument to pass to the callback when a section arrives.
 */
void PESProcessorStopPID(uint16_t pid, PluginPESProcessor_t callback, void *userarg);

/**
 * Destroy all PES Section Processors.
 * @internal
 */
void PESProcessorDestroyAllProcessors(void);
#endif
