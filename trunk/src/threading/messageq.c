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

messageq.h

Thread safe message queue.

*/
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>

#include "messageq.h"
#include "logging.h"
#include "objects.h"
#include "list.h"

/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/

struct MessageQ_s
{
    pthread_mutex_t mutex;
    pthread_cond_t availableCond;
    bool quit;
    List_t *messages;
};

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static char MessageQClass[] = "MessageQ";
static const char MESSAGEQ[] = "MessageQ";

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
MessageQ_t MessageQCreate()
{
    MessageQ_t result;
    ObjectRegisterClass(MessageQClass, sizeof(struct MessageQ_s), NULL);
    
    result = ObjectCreate(MessageQClass);
    if (result)
    {
        result->messages = ListCreate();
        if (result->messages)
        {
            pthread_mutex_init(&result->mutex, NULL);
            pthread_cond_init(&result->availableCond, NULL);
            LogModule(LOG_DEBUG, MESSAGEQ, "Create messageq %p\n", result);
        }
        else
        {
            ObjectRefDec(result);
            result = NULL;
        }
    }
    return result;
}

void MessageQDestroy(MessageQ_t msgQ)
{
    ListIterator_t iterator;
    LogModule(LOG_DEBUG, MESSAGEQ, "Destroying messageq %p\n", msgQ);
    MessageQSetQuit(msgQ);
    pthread_mutex_lock(&msgQ->mutex);
    for (ListIterator_Init(iterator, msgQ->messages); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        void *object = ListIterator_Current(iterator);
        ObjectRefDec(object);
    }
    ListFree(msgQ->messages, NULL);
    pthread_mutex_unlock(&msgQ->mutex);
    pthread_mutex_destroy(&msgQ->mutex);
    pthread_cond_destroy(&msgQ->availableCond);
    ObjectRefDec(msgQ);
    LogModule(LOG_DEBUG, MESSAGEQ, "Destroyed messageq %p\n", msgQ);
}

void MessageQSend(MessageQ_t msgQ, void *msg)
{
    pthread_mutex_lock(&msgQ->mutex);
    if (!msgQ->quit)
    {
        ObjectRefInc(msg);
        ListAdd(msgQ->messages, msg);
        pthread_cond_signal(&msgQ->availableCond);
    }
    pthread_mutex_unlock(&msgQ->mutex);    
}

int MessageQAvailable(MessageQ_t msgQ)
{
    int count = 0;
    pthread_mutex_lock(&msgQ->mutex);
    count = ListCount(msgQ->messages);
    pthread_mutex_unlock(&msgQ->mutex);        
    return count;
}

void *MessageQReceive(MessageQ_t msgQ)
{
    void *result = NULL;
    pthread_mutex_lock(&msgQ->mutex);
    if (!msgQ->quit)
    {
        if (ListCount(msgQ->messages) == 0)
        {
            pthread_cond_wait(&msgQ->availableCond, &msgQ->mutex);
        }
        if (!msgQ->quit)
        {
            ListIterator_t iterator;
            ListIterator_Init(iterator, msgQ->messages);
            result = ListIterator_Current(iterator);
            ListRemoveCurrent(&iterator);
        }
    }
    pthread_mutex_unlock(&msgQ->mutex);       
    return result;
}

void *MessageQReceiveTimed(MessageQ_t msgQ, ulong timeout)
{
    void *result = NULL;
    struct timespec tilltime;

    pthread_mutex_lock(&msgQ->mutex);
    if (!msgQ->quit)
    {
        if (ListCount(msgQ->messages) == 0)
        {
            clock_gettime(CLOCK_REALTIME, &tilltime);
            tilltime.tv_nsec += timeout * 1000000;
            pthread_cond_timedwait(&msgQ->availableCond, &msgQ->mutex, &tilltime);
        }
        if ((!msgQ->quit) && (ListCount(msgQ->messages) > 0))
        {
            ListIterator_t iterator;
            ListIterator_Init(iterator, msgQ->messages);
            result = ListIterator_Current(iterator);
            ListRemoveCurrent(&iterator);
        }
    }
    pthread_mutex_unlock(&msgQ->mutex);       
    return result;
}


void MessageQSetQuit(MessageQ_t msgQ)
{
    pthread_mutex_lock(&msgQ->mutex);
    msgQ->quit = TRUE;
    pthread_cond_signal(&msgQ->availableCond);
    pthread_mutex_unlock(&msgQ->mutex);   
}

void MessageQResetQuit(MessageQ_t msgQ)
{
    pthread_mutex_lock(&msgQ->mutex);
    msgQ->quit = FALSE;
    pthread_mutex_unlock(&msgQ->mutex); 
}

bool MessageQIsQuitSet(MessageQ_t msgQ)
{
    bool result = FALSE;
    pthread_mutex_lock(&msgQ->mutex);
    result = msgQ->quit;
    pthread_mutex_unlock(&msgQ->mutex); 
    return result;
}

