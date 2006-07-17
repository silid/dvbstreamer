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

messages.h

Binary Communications protocol message manipulation functions.

*/

#ifndef _MESSAGES_H
#define _MESSAGES_H
#include <stdint.h>

#define MESSAGE_MAX_LENGTH 0xffff

/* Message Codes */
#include "msgcodes.h"

typedef struct Message_t
{
    int code;
    int length;
    char buffer[MESSAGE_MAX_LENGTH];
    int currentpos;
}Message_t;


int MessageRecv(Message_t *msg, int fromfd);
int MessageSend(Message_t *msg, int tofd);



#define MessageSetCode(_msg, _code) (_msg)->code = (_code)
#define MessageGetCode(_msg) ((_msg)->code & 0xffff)

#define MessageGetLength(_msg) (_msg)->length

#define MessageInit(_msg, _code) \
    do{\
        MessageReset(_msg);\
        MessageSetCode(_msg,_code);\
    }while(0)

/******************************************************************************/
/* High level message formating/parsing functions                             */
/* printf/scanf-style message formating function, but without the % as there  */
/* is no simple/obvious way of interpreting other characters meanings.        */
/* s = string                                                                 */
/* b = uint8                                                                  */
/* d = uint16                                                                 */
/* l = uint32                                                                 */
/******************************************************************************/
int MessageEncode(Message_t *msg, char *format, ...);
int MessageDecode(Message_t *msg, char *format, ...);

/******************************************************************************/
/* Low Level Message writing/reading functions                                */
/* Return 0 if ok, 1 if reading/writing the field would overrun the message   */
/* length.                                                                    */
/******************************************************************************/
int MessageReadString(Message_t *msg, char **result);
int MessageReadUint8(Message_t *msg, uint8_t *result);
int MessageReadUint16(Message_t *msg, uint16_t *result);
int MessageReadUint32(Message_t *msg, uint32_t *result);

int MessageWriteString(Message_t *msg, char *towrite);
int MessageWriteUint8(Message_t *msg, uint8_t towrite);
int MessageWriteUint16(Message_t *msg, uint16_t towrite);
int MessageWriteUint32(Message_t *msg, uint32_t towrite);

void MessageReset(Message_t *msg);
void MessageSeek(Message_t *msg, int offset);

void MessageRERR(Message_t *msg, char errcode, char *str);
#endif
