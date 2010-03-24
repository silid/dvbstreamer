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

dispatchers.h

File descriptor monitoring and event dispatching.

*/
#ifndef _DISPATCHERS_H
#define _DISPATCHERS_H
#include <ev.h>

int DispatchersInit(void);
int DispatchersDeInit(void);
void DispatchersStart(bool sync);
void DispatchersExitLoop(void);
void DispatchersStop(void);
struct ev_loop * DispatchersGetInput(void);
struct ev_loop * DispatchersGetNetwork(void);
struct ev_loop * DispatchersGetUserInput(void);
#endif
