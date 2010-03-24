/*
Copyright (C) 2009  Adam Charrett

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

dispatchers.c

File Descriptor monitoring and event dispatching.

*/
#include <stdlib.h>
#include <string.h>

#include "logging.h"
#include "dispatchers.h"

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void *InputDispatcher(void*arg);
static void *UserNetDispatcher(void*arg);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static char DISPATCHERS[] = "Dispatchers";
static struct ev_loop *InputEventLoop = NULL;
static struct ev_loop *UserNetEventLoop = NULL;
static pthread_t InputDispatcherThread;
static pthread_t UserNetDispatcherThread;
static bool UserNetSync = FALSE;

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/


int DispatchersInit(void)
{
    UserNetEventLoop = ev_loop_new(EVFLAG_AUTO);
    InputEventLoop = ev_loop_new(EVFLAG_AUTO);
    return 0;
}

int DispatchersDeInit(void)
{
    ev_loop_destroy(InputEventLoop);
    ev_loop_destroy(UserNetEventLoop);
    return 0;
}

void DispatchersStart(bool sync)
{
    pthread_create(&InputDispatcherThread, NULL, InputDispatcher, NULL);
    if (sync)
    {
        ev_loop(UserNetEventLoop, 0);
    }
    else
    {
        pthread_create(&UserNetDispatcherThread, NULL, UserNetDispatcher, NULL);
    }
    UserNetSync = sync;
}

void DispatchersExitLoop(void)
{
    ev_unloop(InputEventLoop, 2);
    ev_unloop(UserNetEventLoop, 2);
}

void DispatchersStop(void)
{
    ev_unloop(InputEventLoop, 2);
    ev_unloop(UserNetEventLoop, 2);
    pthread_join(InputDispatcherThread, NULL);
    if (!UserNetSync)
    {
        pthread_join(UserNetDispatcherThread, NULL);
    }
}

struct ev_loop * DispatchersGetInput(void)
{
    return InputEventLoop;
}

struct ev_loop * DispatchersGetNetwork(void)
{
    return UserNetEventLoop;
}

struct ev_loop * DispatchersGetUserInput(void)
{
    return UserNetEventLoop;    
}

static void *InputDispatcher(void*arg)
{
    LogRegisterThread(pthread_self(), "InputDispatcher");
    LogModule(LOG_INFOV, DISPATCHERS, "Input dispatcher started");
    ev_loop(InputEventLoop, 0);
    LogModule(LOG_INFOV, DISPATCHERS, "Input dispatcher finished");    
    return NULL;
}

static void *UserNetDispatcher(void*arg)
{
    LogRegisterThread(pthread_self(), "NetDispatcher");
    LogModule(LOG_INFOV, DISPATCHERS, "Network dispatcher started");
    ev_loop(UserNetEventLoop, 0);
    LogModule(LOG_INFOV, DISPATCHERS, "Network dispatcher finished");    
    return NULL;
}


