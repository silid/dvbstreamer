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

#ifndef _DVBSTREAMER_MESSAGEQ_H
#define _DVBSTREAMER_MESSAGEQ_H

#include "types.h"

/**
 * @defgroup MessageQ Thread safe message queue
 *@{
 */

/**
 * Message Queue handle.
 */
typedef struct MessageQ_s *MessageQ_t;

/**
 * Creates a new double linked list.
 * @return A new MessageQ_t instance or NULL if there is not enough memory.
 */
MessageQ_t MessageQCreate();

/**
 * Frees the specified message queue.
 * @param msgQ The message queue to free.
 */
void MessageQDestroy(MessageQ_t msgQ);

/**
 * Sends the specified msg to the message queue. msg must be allocated
 * using Object(Alloc/Create) as on adding to the queue they will have their 
 * reference count increased using ObjectRefInc and any messages are left in the 
 * queue when it is destroyed they will first be unref'ed using ObjectRefDec().
 *
 * @param msgQ The message queue to send the message to.
 * @param msg The object to send.
 */
void MessageQSend(MessageQ_t msgQ, void *msg);

/**
 * Returns the number of messages waiting in the queue.
 * @param msgQ The message queue to count the waiting messages in.
 * @return The number of waiting messages.
 */
int MessageQAvailable(MessageQ_t msgQ);

/**
 * Receive a message from the queue, the return object should be unref'ed once
 * finished with via ObjectRefDec.
 * If the quit flag is set on the message queue NULL will be returned.
 * @param msgQ The queue to receive a message from.
 * @return A pointer to an object or NULL if quit is set.
 */
void *MessageQReceive(MessageQ_t msgQ);

/**
 * Receive a message from the queue waiting for upto timeout milliseconds. 
 * The return object should be unref'ed once finished with via ObjectRefDec.
 * If the quit flag is set on the message queue NULL will be returned.
 * @param msgQ The queue to receive a message from.
 * @param timeout The number of milliseconds to wait for a message.
 * @return A pointer to an object or NULL if quit is set.
 */

void *MessageQReceiveTimed(MessageQ_t msgQ, ulong timeout);

/**
 * Set the quit flag on the specified message queue. Once set all current and 
 * future calls to MessageQReceive will return NULL.
 * @param msgQ The message queue to set the quit flag on.
 */
void MessageQSetQuit(MessageQ_t msgQ);

/** 
 * Reset the quit flag on the specified message queue so messages can once again 
 * be received.
 * @param msgQ The message queue to reset the quit flag on.
 */
void MessageQResetQuit(MessageQ_t msgQ);

/**
 * Test whether the quit flag is set on the specified message queue.
 * @param msgQ The message queue to check.
 * @return TRUE if the quit flag is set, FALSE otherwise.
 */
bool MessageQIsQuitSet(MessageQ_t msgQ);

/** @} */
#endif

