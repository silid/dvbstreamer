import socket
import struct


# Low level data formats
FORMAT_UINT8  = 'B'
FORMAT_UINT16 = '>H'
FORMAT_UINT32 = '>I'
FORMAT_HEADER = '>BH'

# Base port the DVBStreamer server run on ( + adpter number)
PORT = 54197

# Message Codes
MSGCODE_RERR = 0xFF # = 0xFF = RERR = Response Error   
MSGCODE_INFO = 0x00 # = 0x00 = INFO = Information  
MSGCODE_AUTH = 0x01 # = 0x01 = AUTH = Authenticate  
MSGCODE_CSPS = 0x11 # = 0x11 = CSPS = Control Service Primary Select - Select Primary Service  
MSGCODE_CSSA = 0x12 # = 0x12 = CSSA = Control Service Secondary Add - Add secondary service  
MSGCODE_CSSS = 0x13 # = 0x12 = CSSA = Control Service Secondary Select - Select service to stream  
MSGCODE_CSSR = 0x14 # = 0x13 = CSSR = Control Service Secondary Remove - Remove secondary service  
MSGCODE_COAO = 0x15 # = 0x14 = COAO = Control Output Add Output - Add a new output destination  
MSGCODE_CORO = 0x16 # = 0x15 = CORO = Control Output Remove Output - Remove an output destination  
MSGCODE_COAP = 0x17 # = 0x16 = COAP = Control Output Add PIDs - Add pids to an output.  
MSGCODE_CORP = 0x18 # = 0x17 = CORP = Control Output remove PIDs - Remove pids from an output.  
MSGCODE_SSPC = 0x21 # = 0x21 = SSPC = Status Service Primary Current - Return current service name for primary output.  
MSGCODE_SSSL = 0x22 # = 0x22 = SSSL = Status Service Secondary List - List secondary outputs.  
MSGCODE_SOLO = 0x23 # = 0x23 = SOLO = Status Outputs List outputs  
MSGCODE_SOLP = 0x24 # = 0x24 = SOLP = Status Output List pids  
MSGCODE_SOPC = 0x25 # = 0x25 = SOPC = Status Output Packet Count  
MSGCODE_STSS = 0x26 # = 0x26 = STSS = Status TS Stats  
MSGCODE_SFES = 0x27 # = 0x27 = SFES = Status Front End Status  
MSGCODE_SSLA = 0x28 # = 0x28 = SSLA = Status Services List All - List all avaialable services  
MSGCODE_SSLM = 0x29 # = 0x29 = SSLM = Status Services List Multiplex - List services avaialable of the current multiplex  
MSGCODE_SSPL = 0x2A # = 0x2A = SSPL = Status Services List PIDs  
MSGCODE_RSSL = 0x31 # = 0x31 = RSSL = Response Service Secondary List  
MSGCODE_ROLO = 0x32 # = 0x32 = ROLO = Response Outputs List outputs  
MSGCODE_RLP  = 0x33 # = 0x33 = RLP  = Response Output List pids  
MSGCODE_ROPC = 0x34 # = 0x34 = ROPC = Response Output Packet Count  
MSGCODE_RTSS = 0x35 # = 0x35 = RTSS = Response TS Stats  
MSGCODE_RFES = 0x36 # = 0x36 = RFES = Response Front End Status  
MSGCODE_RSL  = 0x37 # = 0x37 = RSL  = Response Services List  

# Error Codes
RERR_OK               = 0x00
RERR_NOTAUTHORISED    = 0x01
RERR_ALREADYEXISTS    = 0x02
RERR_NOSUCHOUTPUT     = 0x03
RERR_ALREADYSTREAMING = 0x04
RERR_UNDEFINED        = 0xff

# Info codes
INFO_NAME             = 0x00
INFO_FETYPE           = 0x01
INFO_AUTHENTICATED    = 0x02
INFO_UPTIMESEC        = 0xFE
INFO_UPTIME           = 0xFF


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
    header = socket.recv(3)
    (type, length) =  struct.unpack(FORMAT_HEADER, header)
    body = socket.recv(length)
    
    return (type, body)

def MessageDecode(type, body):
    if type == MSGCODE_RERR : # = 0xFF = RERR = Response Error  
        (code, body) = MessageReadUInt8(body)
        (str,  body) = MessageReadString(body)
        return (code, str)
    if type == MSGCODE_RSSL : # = 0x31 = RSSL = Response Service Secondary List  
        pass
    if type == MSGCODE_ROLO : # = 0x32 = ROLO = Response Outputs List outputs  
        pass
    if type == MSGCODE_RLP  : # = 0x33 = RLP  = Response Output List pids  
        pass
    if type == MSGCODE_ROPC : # = 0x34 = ROPC = Response Output Packet Count  
        pass
    if type == MSGCODE_RTSS : # = 0x35 = RTSS = Response TS Stats  
        pass
    if type == MSGCODE_RFES : # = 0x36 = RFES = Response Front End Status  
        pass
    if type == MSGCODE_RSL  : # = 0x37 = RSL  = Response Services List      
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
    
    if type == MSGCODE_INFO : # = 0x00 = INFO = Information  
        body = MessageWriteUInt8(body, args[0])
        
    elif ((type == MSGCODE_AUTH) or # = 0x01 = AUTH = Authenticate  
         (type == MSGCODE_CSSA) or # = 0x12 = CSSA = Control Service Secondary Add - Add secondary service  
         (type == MSGCODE_CSSS) or # = 0x12 = CSSA = Control Service Secondary Select - Select service to stream  
         (type == MSGCODE_COAO)):   # = 0x14 = COAO = Control Output Add Output - Add a new output destination  
        body = MessageWriteString(body, args[0]) # Username, Service Output name, Service Output name, Output name
        body = MessageWriteString(body, args[1]) # Password, IP:PORT, Service Name, IP: Port
        
    elif ((type == MSGCODE_CSPS) or # = 0x11 = CSPS = Control Service Primary Select - Select Primary Service  
         (type == MSGCODE_CSSR) or # = 0x13 = CSSR = Control Service Secondary Remove - Remove secondary service  
         (type == MSGCODE_CORO) or # = 0x15 = CORO = Control Output Remove Output - Remove an output destination 
         (type == MSGCODE_SOLP) or # = 0x24 = SOLP = Status Output List pids 
         (type == MSGCODE_SOPC) or # = 0x25 = SOPC = Status Output Packet Count 
         (type == MSGCODE_SSPL)) : # = 0x2A = SSPL = Status Services List PIDs  
        body = MessageWriteString(body, args[0]) # Service name,  Service Output name, Output name

    elif ((type == MSGCODE_COAP) or # = 0x16 = COAP = Control Output Add PIDs - Add pids to an output.  
         (type == MSGCODE_CORP)) :  # = 0x17 = CORP = Control Output remove PIDs - Remove pids from an output.  
        body = MessageWriteString(body, args[0]) # Output name
        pids = arg[1]
        nrofpids = len(pids)

        if nrofpids > 255:
            nrofpids = 255 # Should signal an error here!
            
        body = MessageWriteUInt8(body, nrofpids)
        # Add each pid to the body
        for pid in pids:
            body = MessageWriteUInt16(body, pid)

    elif ((type == MSGCODE_SSPC) or # = 0x21 = SSPC = Status Service Primary Current - Return current service name for primary output.  
         (type == MSGCODE_SSSL) or # = 0x22 = SSSL = Status Service Secondary List - List secondary outputs.  
         (type == MSGCODE_SOLO) or # = 0x23 = SOLO = Status Outputs List outputs  
         (type == MSGCODE_STSS) or # = 0x26 = STSS = Status TS Stats  
         (type == MSGCODE_SFES) or # = 0x27 = SFES = Status Front End Status  
         (type == MSGCODE_SSLA) or # = 0x28 = SSLA = Status Services List All - List all avaialable services  
         (type == MSGCODE_SSLM)) : # = 0x29 = SSLM = Status Services List Multiplex - List services avaialable of the current multiplex  
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
    
    print "Type %d (length %d) Code %d String \'%s\'" % (type, length, code, str)
    
    body = MessageEncode(MSGCODE_AUTH, "adam", "test")
    MessageSend(s, MSGCODE_AUTH, body )
    
    (type, body) = MessageRecv(s)
    length = len(body)
    (code, str) = MessageDecode(type, body)
    
    print "Type %d (length %d) Code %d String \'%s\'" % (type, length, code, str)
    
    
    body = MessageEncode(MSGCODE_INFO, INFO_AUTHENTICATED)
    MessageSend(s, MSGCODE_INFO, body )
    
    (type, body) = MessageRecv(s)
    length = len(body)
    (code, str) = MessageDecode(type, body)
    
    print "Type %d (length %d) Code %d String \'%s\'" % (type, length, code, str)
    
    body = MessageEncode(MSGCODE_AUTH, "adam", "t1est")
    MessageSend(s, MSGCODE_AUTH, body )
    
    (type, body) = MessageRecv(s)
    length = len(body)
    (code, str) = MessageDecode(type, body)
    
    print "Type %d (length %d) Code %d String \'%s\'" % (type, length, code, str)
    
    
    body = MessageEncode(MSGCODE_INFO, INFO_AUTHENTICATED)
    MessageSend(s, MSGCODE_INFO, body )
    
    (type, body) = MessageRecv(s)
    length = len(body)
    (code, str) = MessageDecode(type, body)
    
    print "Type %d (length %d) Code %d String \'%s\'" % (type, length, code, str)
    
    
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