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
 
messages.c
 
Binary Communications protocol message manipulation functions.
 
*/
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "messages.h"

#define MESSAGE_CHECKDATAAVAILABLE(_msg, _requiredlen) \
    do{ \
        if (((_msg)->currentpos + (_requiredlen)) > (_msg)->length) \
        { \
            return 1; \
        } \
    }while(0)

#define MESSAGE_CHECKSPACEAVAILABLE(_msg, _requiredlen) \
    do{ \
        if (((_msg)->currentpos + (_requiredlen)) > MESSAGE_MAX_LENGTH) \
        { \
            return 1; \
        } \
    }while(0)
    
#define MESSAGE_UPDATELENGTHPOS(_msg, _add) \
    do{ \
        if (((_msg)->currentpos + (_add)) > (_msg)->length) \
        { \
            (_msg)->length = (_msg)->currentpos + (_add); \
        } \
        (_msg)->currentpos += _add; \
    }while(0)
    
    

/* Functions to read/write data from/to a message buffer.
   return 0 if ok, 1 if reading/writing the field would overrun the message length.
*/

int MessageRecv(Message_t *msg, int fromfd)
{
    char header[3];
    if (read(fromfd, header, sizeof(header)) != sizeof(header))
    {
        /* Socket must be dead exit this connection! */
        return 1;
    }
    
    msg->code = header[0];
    msg->length = (header[1] << 8) | header[2];
    msg->currentpos = 0;
    if (read(fromfd, msg->buffer, msg->length) != msg->length)
    {
        /* Socket must be dead exit this connection! */
        return 1;
    }
    return 0;
}

int MessageSend(Message_t *msg, int tofd)
{
    char header[3];
    header[0] = msg->code;
    header[1] = (msg->length >> 8) & 0xff;
    header[2] = (msg->length     ) & 0xff;
    
    if (write(tofd, header, sizeof(header)) != sizeof(header))
    {
        /* Socket must be dead exit this connection! */
        return 1;
    }
    
    if (write(tofd, msg->buffer, msg->length) != msg->length)
    {
        /* Socket must be dead exit this connection! */
        return 1;
    }
    
    return 0;
}

int MessageReadString(Message_t *msg, char **result)
{
    char *str = NULL;
    int size = 0;

    MESSAGE_CHECKDATAAVAILABLE(msg, 1);

    size = msg->buffer[msg->currentpos];
    msg->currentpos ++;
    
    MESSAGE_CHECKDATAAVAILABLE(msg, size);
    
    str = malloc(size + 1);
    if (!str)
    {
        *result = NULL;
        msg->currentpos += size;    
        return 0;
    }
    str[size] = 0;
    memcpy(str, &msg->buffer[msg->currentpos], size);
    msg->currentpos += size;
    *result = str;
    return 0;
}

int MessageReadUint8(Message_t *msg, uint8_t *result)
{
    MESSAGE_CHECKDATAAVAILABLE(msg, 1);
    
    *result = msg->buffer[msg->currentpos];
    msg->currentpos ++;
    return 0;
}


int MessageReadUint16(Message_t *msg, uint16_t *result)
{
    MESSAGE_CHECKDATAAVAILABLE(msg, 2);

    *result = (msg->buffer[msg->currentpos] << 8) | msg->buffer[msg->currentpos + 1];
    msg->currentpos += 2;
    return 0;
}

int MessageReadUint32(Message_t *msg, uint32_t *result)
{
    MESSAGE_CHECKDATAAVAILABLE(msg, 4);
    
    *result = (msg->buffer[msg->currentpos    ] << 24) | 
              (msg->buffer[msg->currentpos + 1] << 16) |
              (msg->buffer[msg->currentpos + 2] << 16) |
              (msg->buffer[msg->currentpos + 3]);
    msg->currentpos += 4;
    return 0;
}

int MessageWriteString(Message_t *msg, char *towrite)
{
    int len = strlen(towrite) & 0xff;

    MESSAGE_CHECKSPACEAVAILABLE(msg, 1 + len);

    msg->buffer[msg->currentpos] = len;
    
    memcpy(&msg->buffer[msg->currentpos + 1], towrite, len);
    
    MESSAGE_UPDATELENGTHPOS(msg, 1 + len);
    return 0;
}

int MessageWriteUint8(Message_t *msg, uint8_t towrite)
{
    MESSAGE_CHECKSPACEAVAILABLE(msg, 1);

    msg->buffer[msg->currentpos] = towrite;
    
    MESSAGE_UPDATELENGTHPOS(msg, 1);
    return 0;
}

int MessageWriteUint16(Message_t *msg, uint16_t towrite)
{
    MESSAGE_CHECKSPACEAVAILABLE(msg, 2);

    msg->buffer[msg->currentpos    ] = (towrite >> 8);
    msg->buffer[msg->currentpos + 1] = towrite & 0xff;
    
    MESSAGE_UPDATELENGTHPOS(msg, 2);
    return 0;
}

int MessageWriteUint32(Message_t *msg, uint32_t towrite)
{
    MESSAGE_CHECKSPACEAVAILABLE(msg, 4);

    msg->buffer[msg->currentpos    ] = (towrite >> 24);
    msg->buffer[msg->currentpos + 1] = (towrite >> 16);
    msg->buffer[msg->currentpos + 2] = (towrite >>  8);
    msg->buffer[msg->currentpos + 3] = towrite & 0xff;
    
    MESSAGE_UPDATELENGTHPOS(msg, 4);
    return 0;
}

void MessageReset(Message_t *msg)
{
    msg->length = 0;
    msg->currentpos = 0;
}

void MessageSeek(Message_t *msg, int offset)
{
    if (offset > msg->length)
    {
        msg->length = offset;
    }
    msg->currentpos = offset;
}

void MessageRERR(Message_t *msg, char errcode, char *str)
{
    MessageReset(msg);
    msg->code = MSGCODE_RERR;
    MessageWriteUint8(msg, (uint8_t)errcode);
    if (!str)
    {
        str = "";
    }
    MessageWriteString(msg, str);
}
