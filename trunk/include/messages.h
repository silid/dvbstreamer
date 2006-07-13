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
#define MSGCODE_RERR 0xFF /* 0xFF = RERR = Response Error  */
#define MSGCODE_INFO 0x00 /* 0x01 = INFO = Information */
#define MSGCODE_AUTH 0x01 /* 0x01 = AUTH = Authenticate */
#define MSGCODE_CSPS 0x11 /* 0x11 = CSPS = Control Service Primary Select - Select Primary Service */
#define MSGCODE_CSSA 0x12 /* 0x12 = CSSA = Control Service Secondary Add - Add secondary service */
#define MSGCODE_CSSS 0x13 /* 0x12 = CSSA = Control Service Secondary Select - Select service to stream */
#define MSGCODE_CSSR 0x14 /* 0x13 = CSSR = Control Service Secondary Remove - Remove secondary service */
#define MSGCODE_COAO 0x15 /* 0x14 = COAO = Control Output Add Output - Add a new output destination */
#define MSGCODE_CORO 0x16 /* 0x15 = CORO = Control Output Remove Output - Remove an output destination */
#define MSGCODE_COAP 0x17 /* 0x16 = COAP = Control Output Add PIDs - Add pids to an output. */
#define MSGCODE_CORP 0x18 /* 0x17 = CORP = Control Output remove PIDs - Remove pids from an output. */
#define MSGCODE_SSPC 0x21 /* 0x21 = SSPC = Status Service Primary Current - Return current service name for primary output. */
#define MSGCODE_SSSL 0x22 /* 0x22 = SSSL = Status Service Secondary List - List secondary outputs. */
#define MSGCODE_SOLO 0x23 /* 0x23 = SOLO = Status Outputs List outputs */
#define MSGCODE_SOLP 0x24 /* 0x24 = SOLP = Status Output List pids */
#define MSGCODE_SOPC 0x25 /* 0x25 = SOPC = Status Output Packet Count */
#define MSGCODE_STSS 0x26 /* 0x26 = STSS = Status TS Stats */
#define MSGCODE_SFES 0x27 /* 0x27 = SFES = Status Front End Status */
#define MSGCODE_SSLA 0x28 /* 0x28 = SSLA = Status Services List All - List all avaialable services */
#define MSGCODE_SSLM 0x29 /* 0x29 = SSLM = Status Services List Multiplex - List services avaialable of the current multiplex */
#define MSGCODE_SSPL 0x2A /* 0x2A = SSPL = Status Services List PIDs */
#define MSGCODE_RSSL 0x31 /* 0x31 = RSSL = Response Service Secondary List */
#define MSGCODE_ROLO 0x32 /* 0x32 = ROLO = Response Outputs List outputs */
#define MSGCODE_RLP  0x33 /* 0x33 = RLP  = Response List pids */
#define MSGCODE_ROPC 0x34 /* 0x34 = ROPC = Response Output Packet Count */
#define MSGCODE_RTSS 0x35 /* 0x35 = RTSS = Response TS Stats */
#define MSGCODE_RFES 0x36 /* 0x36 = RFES = Response Front End Status */
#define MSGCODE_RLS  0x37 /* 0x37 = RSL  = Response Services List */

#define RERR_OK               0x00
#define RERR_NOTAUTHORISED    0x01
#define RERR_ALREADYEXISTS    0x02
#define RERR_NOTFOUND         0x03
#define RERR_ALREADYSTREAMING 0x04
#define RERR_UNDEFINED        0xff

#define INFO_NAME             0x00
#define INFO_FETYPE           0x01
#define INFO_AUTHENTICATED    0x02
#define INFO_UPTIMESEC        0xFE
#define INFO_UPTIME           0xFF

typedef struct Message_t
{
    char code;
    int length;
    char buffer[MESSAGE_MAX_LENGTH];
    int currentpos;
}Message_t;


int MessageRecv(Message_t *msg, int fromfd);
int MessageSend(Message_t *msg, int tofd);



#define MessageSetCode(_msg, _code) (_msg)->code = (_code)
#define MessageGetCode(_msg) ((_msg)->code & 0xff)

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
/* s = uint16                                                                 */
/* d = uint32                                                                 */
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
