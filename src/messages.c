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
#include <stdarg.h>

#include "logging.h"
#include "types.h"
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




/******************************************************************************/
/* Message Send/Receive functions                                             */
/******************************************************************************/
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

/******************************************************************************/
/* High level message formating/parsing functions                             */
/* printf/scanf-style message formating function, but without the % as there  */
/* is no simple/obvious way of interpreting other characters meanings.        */
/* s = string                                                                 */
/* b = uint8                                                                  */
/* d = uint16                                                                 */
/* l = uint32                                                                 */
/******************************************************************************/
int MessageEncode(Message_t *msg, uint8_t code, char *format, ...)
{
    int paramsEncoded = 0;
    int i;
    va_list args;
    MessageReset(msg);
    MessageSetCode(msg, code);

    va_start(args, format);
    for (i = 0; format[i]; i ++)
    {
        bool failed = FALSE;
        switch (format[i])
        {
            case 's':
            case 'S':
            {
                char *string = va_arg(args, char *);
                failed = MessageWriteString(msg, string);
            }
            break;
            case 'b':
            case 'B':
            {
                uint8_t byte = (uint8_t)va_arg(args, int);
                failed = MessageWriteUint8(msg, byte);
            }
            break;
            case 'd':
            case 'D':
            {
                uint16_t uint16 = (uint16_t)va_arg(args, int);
                failed = MessageWriteUint16(msg, uint16);
            }
            break;
            case 'l':
            case 'L':
            {
                uint32_t uint32 = va_arg(args, uint32_t);
                failed = MessageWriteUint32(msg, uint32);
            }
            break;
            default:
            failed = TRUE;
            break;
        }
        if (failed)
        {
            break;
        }
        paramsEncoded ++;
    }
    va_end(args);
    return paramsEncoded;
}

int MessageDecode(Message_t *msg, char *format, ...)
{
    int paramsDecoded = 0;
    int i;
    va_list args;

    va_start(args, format);
    for (i = 0; format[i]; i ++)
    {
        bool failed = FALSE;
        switch (format[i])
        {
            case 's':
            case 'S':
            {
                char **string = va_arg(args, char **);
                failed = MessageReadString(msg, string);
            }
            break;
            case 'b':
            case 'B':
            {
                uint8_t *byte = va_arg(args, uint8_t *);
                failed = MessageReadUint8(msg, byte);
            }
            break;
            case 'd':
            case 'D':
            {
                uint16_t *uint16 = va_arg(args, uint16_t *);
                failed = MessageReadUint16(msg, uint16);
            }
            break;
            case 'l':
            case 'L':
            {
                uint32_t *uint32 = va_arg(args, uint32_t *);
                failed = MessageReadUint32(msg, uint32);
            }
            break;
            default:
            failed = TRUE;
            break;
        }
        if (failed)
        {
            break;
        }
        paramsDecoded ++;
    }
    va_end(args);
    return paramsDecoded;
}
/******************************************************************************/
/* Low Level Message writing/reading functions                                */
/* Return 0 if ok, 1 if reading/writing the field would overrun the message   */
/* length.                                                                    */
/******************************************************************************/
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
    int len = 0;
    if (towrite)
    {
        len = strlen(towrite) & 0xff;
    }

    MESSAGE_CHECKSPACEAVAILABLE(msg, 1 + len);

    msg->buffer[msg->currentpos] = len;

    if (towrite)
    {
        memcpy(&msg->buffer[msg->currentpos + 1], towrite, len);
    }

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
