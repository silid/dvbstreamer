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

deferredproc.h

Deferred Processing thread.

*/
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>

#include "deferredproc.h"
#include "messageq.h"
#include "logging.h"
#include "objects.h"
#include "list.h"

/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/
typedef struct DeferredJob_s
{
    DeferredProcessor_t processor;
    void *arg;
}DeferredJob_t;

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void *DeferredProcessingThread(void* arg);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static const char DEFERREDPROC[] = "DeferredProc";
static MessageQ_t jobQ = NULL;
static pthread_t processingThread;


/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/

int DeferredProcessingInit(void)
{
    ObjectRegisterType(DeferredJob_t);
    jobQ = MessageQCreate();
    pthread_create(&processingThread, NULL, DeferredProcessingThread, NULL);
    return 0;
}

void DeferredProcessingDeinit(void)
{
    /* Signal thread to exit and wait */
    MessageQSetQuit(jobQ);
    pthread_join(processingThread, NULL);

    /* Destory thread and queue */
    MessageQDestroy(jobQ);
    pthread_detach(processingThread);
    jobQ = NULL;
}

void DeferredProcessingAddJob(DeferredProcessor_t processor, void *arg)
{
    if (jobQ)
    {
        DeferredJob_t *job = ObjectCreateType(DeferredJob_t);
        LogModule(LOG_DEBUGV, DEFERREDPROC, "Adding job %p (processor:%p, arg:%p)\n", job, processor, arg);
        job->processor = processor;
        job->arg = arg;
        ObjectRefInc(arg);
        MessageQSend(jobQ, job);
        ObjectRefDec(job);
    }
}


/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/
static void *DeferredProcessingThread(void* arg)
{
    DeferredJob_t *job;
    LogRegisterThread(processingThread, DEFERREDPROC);    
    LogModule(LOG_DEBUG, DEFERREDPROC, "Deferred processing thread started\n");
    while(!MessageQIsQuitSet(jobQ))
    {
        job = (DeferredJob_t*)MessageQReceive(jobQ);
        if (job)
        {
            LogModule(LOG_DEBUGV, DEFERREDPROC, "Running job %p (processor:%p, arg:%p)\n", job, job->processor, job->arg);            
            job->processor(job->arg);
            LogModule(LOG_DEBUGV, DEFERREDPROC, "Finished job %p (processor:%p, arg:%p)\n", job, job->processor, job->arg);            
            ObjectRefDec(job);
        }
    }
    MessageQResetQuit(jobQ);
    LogModule(LOG_DEBUG, DEFERREDPROC, "Discarding %d jobs\n", MessageQAvailable(jobQ));
    while(MessageQAvailable(jobQ))
    {
        job = (DeferredJob_t*)MessageQReceive(jobQ);
        if (job)
        {
            ObjectRefDec(job->arg);
            ObjectRefDec(job);
        }
    }
    LogModule(LOG_DEBUG, DEFERREDPROC, "Deferred processing thread stopped\n");
    return NULL;   
}
