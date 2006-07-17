# Copyright (C) 2006  Adam Charrett
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
#
# binarycomms.py
#
# Binary Communications protocol message implementation.
#
import socket
import struct

# Message Codes
from msgcodes import *

# Low level data formats
FORMAT_UINT8  = 'B'
FORMAT_UINT16 = '>H'
FORMAT_UINT32 = '>I'
FORMAT_HEADER = '>HH'

# Base port the DVBStreamer server run on ( + adapter number)
PORT = 54197



def MessageReadString(body):
    (len,) = struct.unpack(FORMAT_UINT8,body[0:1])
    str = body[1:(1 + len)]
    return (str, body[(len + 1):]) 
    
def MessageReadUInt8(body):
    (result,) = struct.unpack(FORMAT_UINT8,body[0:1])
    return (result, body[1:])
    
def MessageReadUInt16(body):
    (result,) = struct.unpack(FORMAT_UINT16, body[0:2])
    return (result, body[2:])

def MessageReadUInt32(body):
    (result,) = struct.unpack(FORMAT_UINT32, body[0:4])
    return (result, body[4:])

def MessageWriteString(body, str):
    length = len(str)
    if (length > 255):
        length = 255
        str = str[:255]
    return body + struct.pack(FORMAT_UINT8, length) + str
    
def MessageWriteUInt8(body, val):
    return body + struct.pack(FORMAT_UINT8, val)

def MessageWriteUInt16(body, val):
    return body + struct.pack(FORMAT_UINT16, val)

def MessageWriteUInt32(body, val):
    return body + struct.pack(FORMAT_UINT32, val)

def MessageSend(socket, type, body):
    header = struct.pack(FORMAT_HEADER, type, len(body))
    socket.send(header + body)

def MessageRecv(socket):
    header = socket.recv(struct.calcsize(FORMAT_HEADER))
    (type, length) =  struct.unpack(FORMAT_HEADER, header)
    body = socket.recv(length)
    
    return (type, body)

def MessageDecode(type, body):
    if type == MSGCODE_RERR : # Response Error  
        (code, body) = MessageReadUInt8(body)
        (str,  body) = MessageReadString(body)
        return (code, str)
    if type == MSGCODE_RSSL : # Response Service Secondary List  
        pass
    if type == MSGCODE_ROLO : # Response Outputs List outputs  
        pass
    if type == MSGCODE_RLP  : # Response Output List pids  
        pass
    if type == MSGCODE_ROPC : # Response Output Packet Count  
        pass
    if type == MSGCODE_RTSS : # Response TS Stats  
        pass
    if type == MSGCODE_RFES : # Response Front End Status  
        pass
    if type == MSGCODE_RLS  : # Response Services List      
        (nrofservices, body) = MessageReadUInt16(body)
        i = 0
        services = []
        while (i < nrofservices):
            (service, body) = MessageReadString(body)
            services.append(service)
            i = i + 1
        return (services,)
    
    return ()

def MessageEncode(type, *args):
    """
    Constructs a message body for the specified message type.
    Message fields should be passed in the order in which they are defined.
    ie for MSGCODE_AUTH:
    MessageEncode(MSGCODE_AUTH, username, password)
    """
    body = ''
    
    if type == MSGCODE_INFO : # Information  
        body = MessageWriteUInt8(body, args[0])
        
    elif ((type == MSGCODE_AUTH) or # Authenticate  
         (type == MSGCODE_CSSA) or # Control Service Secondary Add - Add secondary service  
         (type == MSGCODE_CSSS) or # Control Service Secondary Select - Select service to stream  
         (type == MSGCODE_COAO)):   # Control Output Add Output - Add a new output destination  
        body = MessageWriteString(body, args[0]) # Username, Service Output name, Service Output name, Output name
        body = MessageWriteString(body, args[1]) # Password, IP:PORT, Service Name, IP: Port
        
    elif ((type == MSGCODE_CSPS) or # Control Service Primary Select - Select Primary Service  
         (type == MSGCODE_CSSR) or # Control Service Secondary Remove - Remove secondary service  
         (type == MSGCODE_CORO) or # Control Output Remove Output - Remove an output destination 
         (type == MSGCODE_SOLP) or # Status Output List pids 
         (type == MSGCODE_SOPC) or # Status Output Packet Count 
         (type == MSGCODE_SSPL)) : # Status Services List PIDs  
        body = MessageWriteString(body, args[0]) # Service name,  Service Output name, Output name

    elif ((type == MSGCODE_COAP) or # Control Output Add PIDs - Add pids to an output.  
         (type == MSGCODE_CORP)) :  # Control Output remove PIDs - Remove pids from an output.  
        body = MessageWriteString(body, args[0]) # Output name
        pids = arg[1]
        nrofpids = len(pids)

        if nrofpids > 255:
            nrofpids = 255 # Should signal an error here!
            
        body = MessageWriteUInt8(body, nrofpids)
        # Add each pid to the body
        for pid in pids:
            body = MessageWriteUInt16(body, pid)

    elif ((type == MSGCODE_SSPC) or # Status Service Primary Current - Return current service name for primary output.  
         (type == MSGCODE_SSSL) or # Status Service Secondary List - List secondary outputs.  
         (type == MSGCODE_SOLO) or # Status Outputs List outputs  
         (type == MSGCODE_STSS) or # Status TS Stats  
         (type == MSGCODE_SFES) or # Status Front End Status  
         (type == MSGCODE_SSLA) or # Status Services List All - List all avaialable services  
         (type == MSGCODE_SSLM)) : # Status Services List Multiplex - List services avaialable of the current multiplex  
        pass
    else:
        print "Unknown message type to encode %d!\n" % type
    

    return body

def test():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect(('localhost', PORT))
    
    body = MessageEncode(MSGCODE_INFO, INFO_UPTIME)
    MessageSend(s, MSGCODE_INFO, body )
    
    (type, body) = MessageRecv(s)
    length = len(body)
    (code, str) = MessageDecode(type, body)
    
    print "Type 0x%04x (length %d) Code %d String \'%s\'" % (type, length, code, str)
    
    body = MessageEncode(MSGCODE_AUTH, "dvbstreamer", "control")
    MessageSend(s, MSGCODE_AUTH, body )
    
    (type, body) = MessageRecv(s)
    length = len(body)
    (code, str) = MessageDecode(type, body)
    
    print "Type 0x%04x (length %d) Code %d String \'%s\'" % (type, length, code, str)
    
    
    body = MessageEncode(MSGCODE_INFO, INFO_AUTHENTICATED)
    MessageSend(s, MSGCODE_INFO, body )
    
    (type, body) = MessageRecv(s)
    length = len(body)
    (code, str) = MessageDecode(type, body)
    
    print "Type 0x%04x (length %d) Code %d String \'%s\'" % (type, length, code, str)
    
    body = MessageEncode(MSGCODE_AUTH, "adam", "t1est")
    MessageSend(s, MSGCODE_AUTH, body )
    
    (type, body) = MessageRecv(s)
    length = len(body)
    (code, str) = MessageDecode(type, body)
    
    print "Type 0x%04x (length %d) Code %d String \'%s\'" % (type, length, code, str)
    
    
    body = MessageEncode(MSGCODE_INFO, INFO_AUTHENTICATED)
    MessageSend(s, MSGCODE_INFO, body )
    
    (type, body) = MessageRecv(s)
    length = len(body)
    (code, str) = MessageDecode(type, body)
    
    print "Type 0x%04x (length %d) Code %d String \'%s\'" % (type, length, code, str)
    
    
    MessageSend(s, MSGCODE_SSLA, '' )
    
    (type, body) = MessageRecv(s)
    length = len(body)
    contents = MessageDecode(type, body)
    print contents
    
    MessageSend(s, MSGCODE_SSLM, '' )
    
    (type, body) = MessageRecv(s)
    length = len(body)
    contents = MessageDecode(type, body)
    print contents
    
    body = MessageEncode(MSGCODE_SSPL, "BBC ONE")
    MessageSend(s, MSGCODE_SSPL, body)
    
    (type, body) = MessageRecv(s)
    length = len(body)
    contents = MessageDecode(type, body)
    print contents
    
    
    body = MessageEncode(MSGCODE_SSPL, "BBC1 ONE")
    MessageSend(s, MSGCODE_SSPL, body)
    
    (type, body) = MessageRecv(s)
    length = len(body)
    contents = MessageDecode(type, body)
    print contents
    
    s.close()

if __name__ == '__main__':
    test()