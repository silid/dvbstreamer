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

binarycomms.c

Binary Communications protocol for control DVBStreamer.

*/
#include "config.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "ts.h"
#include "udpoutput.h"
#include "outputs.h"
#include "logging.h"
#include "cache.h"
#include "messages.h"
#include "main.h"
#include "binarycomms.h"
#include "deliverymethod.h"
#include "commands.h"

#define MAX_CONNECTIONS 2 /* 1 for monitoring by web and another for control */

#define LOG_MALFORMED(_connection, _field) \
    printlog(LOG_DEBUG, "%s:%d : Malformed message, not enough data in message to read %s field!\n", \
        inet_ntoa((_connection)->clientAddress.sin_addr), (_connection)->clientAddress.sin_port, _field)

#define IFAUTHENTICATED(_dofunc, _connection, _message) \
    do { \
        if ((_connection)->authenticated)\
        {\
            _dofunc(_connection, _message);\
        }\
        else \
        { \
          MessageRERR(_message, RERR_NOTAUTHORISED, "Not authorised!"); \
        }\
    }while(0)


#define MessageRERR(_msg, _errcode, _str) \
    do{\
        MessageInit(_msg, MSGCODE_RERR);\
        MessageEncode(_msg, "bs", _errcode, _str);\
    }while(0)

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#define READSTRING(_var)\
    do { \
        if (MessageReadString(message, &_var))\
        {\
            LOG_MALFORMED(connection, TOSTRING(_var));\
            connection->connected = FALSE;\
            return ;\
        }\
    }while(0)

#define READ2STRINGS(_var1, _var2)\
    do { \
        if (MessageReadString(message, &_var1))\
        {\
            LOG_MALFORMED(connection, TOSTRING(_var1));\
            connection->connected = FALSE;\
            return ;\
        }\
        if (MessageReadString(message, &_var2))\
        {\
            LOG_MALFORMED(connection, TOSTRING(_var2));\
            connection->connected = FALSE;\
            free(_var1);\
            return ;\
        }\
    }while(0)

#define READUINT8(_var)\
    do { \
        if (MessageReadUint8(message, &_var))\
        {\
            LOG_MALFORMED(connection, TOSTRING(_var));\
            connection->connected = FALSE;\
            return ;\
        }\
    }while(0)

#define READUINT16(_var)\
    do { \
        if (MessageReadUint16(message, &_var))\
        {\
            LOG_MALFORMED(connection, TOSTRING(_var));\
            connection->connected = FALSE;\
            return ;\
        }\
    }while(0)

#define READUINT32(_var)\
    do { \
        if (MessageReadUint32(message, &_var))\
        {\
            LOG_MALFORMED(connection, TOSTRING(_var));\
            connection->connected = FALSE;\
            return ;\
        }\
    }while(0)

typedef struct Connection_t
{
    int socketfd;
    struct sockaddr_in clientAddress;
    bool authenticated;
    bool active;
    bool connected;
    pthread_t thread;
    Message_t message;
}
Connection_t;

static void HandleConnection(Connection_t *connection);
static void ProcessMessage(Connection_t *connection, Message_t *message);
static void ProcessInfo(Connection_t *connection, Message_t *message);
static void ProcessAuth(Connection_t *connection, Message_t *message);
static void ProcessQuote(Connection_t *connection, Message_t *message);
static int QuoteMessagePrintf(char *fmt, ...);
static void ProcessPrimaryServiceSelect(Connection_t *connection, Message_t *message);
static void ProcessSecondaryServiceAdd(Connection_t *connection, Message_t *message);
static void ProcessSecondaryServiceSet(Connection_t *connection, Message_t *message);
static void ProcessSecondaryServiceRemove(Connection_t *connection, Message_t *message);
static void ProcessServiceSetDestination(Connection_t *connection, Message_t *message);
static void ProcessOutputAdd(Connection_t *connection, Message_t *message);
static void ProcessOutputRemove(Connection_t *connection, Message_t *message);
static void ProcessOutputPIDAdd(Connection_t *connection, Message_t *message);
static void ProcessOutputPIDRemove(Connection_t *connection, Message_t *message);
static void ProcessOutputSetDestination(Connection_t *connection, Message_t *message);
static void ProcessPrimaryServiceCurrent(Connection_t *connection, Message_t *message);
static void ProcessSecondaryServiceList(Connection_t *connection, Message_t *message);
static void ProcessOutputsList(Connection_t *connection, Message_t *message);
static void ProcessOutputListPids(Connection_t *connection, Message_t *message);
static void ProcessServiceFilterPacketCount(Connection_t *connection, Message_t *message);
static void ProcessOutputPacketCount(Connection_t *connection, Message_t *message);
static void ProcessTSStats(Connection_t *connection, Message_t *message);
static void ProcessFEStatus(Connection_t *connection, Message_t *message);
static void ProcessServiceList(Connection_t *connection, Message_t *message, int all);
static void ProcessServicePids(Connection_t *connection, Message_t *message);

static int activeConnections = 0;
static pthread_mutex_t activeConnectionsMutex = PTHREAD_MUTEX_INITIALIZER;
static int serverSocket;
static Connection_t connections[MAX_CONNECTIONS];
static char *infoStreamerName;
static char *authUsername;
static char *authPassword;
static Message_t * QuoteMessage;

static time_t serverStartTime;

int BinaryCommsInit(int adapter, char *streamername, char *username, char *password)
{
    struct sockaddr_in serverAddress;
    serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket < 0)
    {
        printlog(LOG_ERROR, "Failed to create server socket!\n");
        return 1;
    }

    bzero((char *) &serverAddress, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(BINARYCOMMS_PORT + adapter);
    if (bind(serverSocket, (struct sockaddr *) &serverAddress, sizeof(serverAddress)) < 0)
    {
        printlog(LOG_ERROR, "Failed to bind server to port %d", BINARYCOMMS_PORT + adapter);
        close(serverSocket);
        return 1;
    }
    listen(serverSocket, 1);

    infoStreamerName = strdup(streamername);
    authUsername = strdup(username);
    authPassword = strdup(password);

    time(&serverStartTime);
    printlog(LOG_INFO, "Server created %s", ctime(&serverStartTime));
    printlog(LOG_DEBUG, "Username    : %s\n", authUsername);
    printlog(LOG_DEBUG, "Password    : %s\n", authPassword);
    printlog(LOG_DEBUG, "Server Name : %s\n", infoStreamerName);
    return 0;
}

void BinaryCommsDeInit(void)
{
    int i;
    close(serverSocket);
    for ( i = 0; i < MAX_CONNECTIONS; i ++)
    {
        if (connections[i].connected)
        {
            /* Force closure of the sockets should cause the thread to terminate. */
            close(connections[i].socketfd);
            connections[i].connected = FALSE;
        }
    }
}

void BinaryCommsAcceptConnections(void)
{
    struct pollfd pfd[1];

    pfd[0].fd = serverSocket;
    pfd[0].events = POLLIN;

    while (!ExitProgram)
    {
        if (poll(pfd, 1, 200))
        {
            if (pfd[0].revents & POLLIN)
            {
                int clientfd;
                struct sockaddr_in clientAddress;
                int clientAddressSize;

                clientAddressSize = sizeof(clientAddress);
                clientfd = accept(serverSocket, (struct sockaddr *) & clientAddress, &clientAddressSize);
                pthread_mutex_lock(&activeConnectionsMutex);
                if (activeConnections >= MAX_CONNECTIONS)
                {
                    printlog(LOG_INFO, "Connection attempt from %s:%d rejected as too many open connections!\n",
                             inet_ntoa(clientAddress.sin_addr), clientAddress.sin_port);
                    close(clientfd);
                }
                else
                {
                    int found = -1;
                    int i;
                    for (i = 0; i < MAX_CONNECTIONS; i ++)
                    {
                        if (!connections[i].active)
                        {
                            found = i;
                            break;
                        }
                    }
                    if (found >= 0)
                    {
                        connections[i].active = TRUE;
                        connections[i].connected = TRUE;
                        connections[i].socketfd = clientfd;
                        connections[i].clientAddress = clientAddress;
                        activeConnections ++;
                        printlog(LOG_INFO, "Connection attempt from %s:%d accepted!\n",
                                 inet_ntoa(clientAddress.sin_addr), clientAddress.sin_port);
                        pthread_create(&connections[i].thread, NULL, (void*)HandleConnection, (void*)&connections[i]);
                    }
                    else
                    {
                        printlog(LOG_INFO, "Connection attempt from %s:%d rejected as no connections structures left!\n",
                                 inet_ntoa(clientAddress.sin_addr), clientAddress.sin_port);
                        close(clientfd);
                    }

                }
                pthread_mutex_unlock(&activeConnectionsMutex);
            }
        }

    }
}

static void HandleConnection(Connection_t *connection)
{
    struct pollfd pfd[1];
    int socketfd = connection->socketfd;

    pfd[0].fd = socketfd;
    pfd[0].events = POLLIN;

    Message_t *message = &connection->message;

    while (!ExitProgram && connection->connected)
    {
        if (MessageRecv(message, socketfd))
        {
            /* Socket must be dead exit this connection! */
            break;
        }
        ProcessMessage(connection, message);
        if (connection->connected)
        {
            printlog(LOG_DEBUG, "%s:%d : Response 0x%04x length %d\n",
                     inet_ntoa(connection->clientAddress.sin_addr), connection->clientAddress.sin_port,
                     MessageGetCode(message), MessageGetLength(message));
            if (MessageSend(message, socketfd))
            {
                /* Socket must be dead exit this connection! */
                break;
            }
        }
    }
    printlog(LOG_INFO, "%s:%d : Connection closed!\n",
             inet_ntoa(connection->clientAddress.sin_addr), connection->clientAddress.sin_port);
    /* Close the socket and free our resources */
    close(socketfd);
    connection->connected = FALSE;

    pthread_mutex_lock(&activeConnectionsMutex);
    connection->active = FALSE;
    activeConnections --;
    pthread_mutex_unlock(&activeConnectionsMutex);
}

static void ProcessMessage(Connection_t *connection, Message_t *message)
{
    printlog(LOG_DEBUG, "%s:%d : Processing message 0x%04x length %d (%s)\n",
             inet_ntoa(connection->clientAddress.sin_addr), connection->clientAddress.sin_port,
             MessageGetCode(message), MessageGetLength(message));
    switch (message->code)
    {
        case MSGCODE_INFO:
            ProcessInfo(connection, message);
            break;
        case MSGCODE_AUTH:
            ProcessAuth(connection, message);
            break;
        case MSGCODE_QUOT:
            IFAUTHENTICATED(ProcessQuote, connection, message);
            break;
            /* Control Messages */
        case MSGCODE_CSPS:
            IFAUTHENTICATED(ProcessPrimaryServiceSelect, connection, message);
            break;
        case MSGCODE_CSSA:
            IFAUTHENTICATED(ProcessSecondaryServiceAdd, connection, message);
            break;
        case MSGCODE_CSSS:
            IFAUTHENTICATED(ProcessSecondaryServiceSet, connection, message);
            break;
        case MSGCODE_CSSR:
            IFAUTHENTICATED(ProcessSecondaryServiceRemove, connection, message);
            break;
        case MSGCODE_CSSD:
            IFAUTHENTICATED(ProcessServiceSetDestination, connection, message);
            break;
        case MSGCODE_COAO:
            IFAUTHENTICATED(ProcessOutputAdd, connection, message);
            break;
        case MSGCODE_CORO:
            IFAUTHENTICATED(ProcessOutputRemove, connection, message);
            break;
        case MSGCODE_COAP:
            IFAUTHENTICATED(ProcessOutputPIDAdd, connection, message);
            break;
        case MSGCODE_CORP:
            IFAUTHENTICATED(ProcessOutputPIDRemove, connection, message);
            break;
        case MSGCODE_COSD:
            IFAUTHENTICATED(ProcessOutputSetDestination, connection, message);
            break;
            /* Status Messages */
        case MSGCODE_SSPS:
            ProcessPrimaryServiceCurrent(connection, message);
            break;
        case MSGCODE_SSFL:
            ProcessSecondaryServiceList(connection, message);
            break;
        case MSGCODE_SSPC:
            ProcessServiceFilterPacketCount(connection, message);
            break;
        case MSGCODE_SOLO:
            ProcessOutputsList(connection, message);
            break;
        case MSGCODE_SOLP:
            ProcessOutputListPids(connection, message);
            break;
        case MSGCODE_SOPC:
            ProcessOutputPacketCount(connection, message);
            break;
        case MSGCODE_STSS:
            ProcessTSStats(connection, message);
            break;
        case MSGCODE_SFES:
            ProcessFEStatus(connection, message);
            break;
        case MSGCODE_SSLA:
            ProcessServiceList(connection, message, 1);
            break;
        case MSGCODE_SSLM:
            ProcessServiceList(connection, message, 0);
            break;
        case MSGCODE_SSPL:
            ProcessServicePids(connection, message);
            break;
        default:
            MessageRERR(message, RERR_GENERIC, "Unknown message type!");
            break;
    }
}

static void ProcessInfo(Connection_t *connection, Message_t *message)
{
    uint8_t info;
    READUINT8(info);

    switch (info)
    {
        case INFO_NAME:
            MessageRERR(message, RERR_OK, infoStreamerName);
            break;

        case INFO_FETYPE:
            MessageRERR(message, RERR_OK, "Not implemented!");
            break;

        case INFO_AUTHENTICATED:
            MessageRERR(message, RERR_OK, connection->authenticated ? "Authenticated" : "Not authenticated");
            break;

        case INFO_UPSECS:
            {
                char buffer[11];
                time_t now;
                time(&now);

                sprintf(buffer, "%d", (int)difftime(now, serverStartTime));
                MessageRERR(message, RERR_OK, buffer);
            }
            break;

        case INFO_UPTIME:
            {
                char buffer[50];
                time_t now;
                int seconds;
                int d, h, m, s;
                time(&now);
                seconds = (int)difftime(now, serverStartTime);
                d = seconds / (24 * 60 * 60);
                h = (seconds - (d * 24 * 60 * 60)) / (60 * 60);
                m = (seconds - ((d * 24 * 60 * 60) + (h * 60 * 60))) / 60;
                s = (seconds - ((d * 24 * 60 * 60) + (h * 60 * 60) + (m * 60)));
                sprintf(buffer, "%d Days %d Hours %d Minutes %d seconds", d, h, m, s);
                MessageRERR(message, RERR_OK, buffer);
            }
            break;

        default:
            MessageRERR(message, RERR_GENERIC, "Unknown field");
            break;
    }
}

static void ProcessAuth(Connection_t *connection, Message_t *message)
{
    char *msgUsername = NULL;
    char *msgPassword = NULL;

    READ2STRINGS(msgUsername, msgPassword);

    connection->authenticated = (strcmp(msgUsername, authUsername) == 0) &&
                                (strcmp(msgPassword, authPassword) == 0);

    MessageRERR(message, connection->authenticated ? RERR_OK : RERR_NOTAUTHORISED, NULL);
    free(msgUsername);
    free(msgPassword);
}

static void ProcessQuote(Connection_t *connection, Message_t *message)
{
    char *command = NULL;

    READSTRING(command);
    MessageReset(message);
    MessageSetCode(message, MSGCODE_RTXT);
    QuoteMessage = message;
    CommandPrintf = QuoteMessagePrintf;
    if (!CommandExecute( command))
    {
        MessageRERR(message, RERR_GENERIC, "Unknown command");
    }
    free(command);

}

static int QuoteMessagePrintf(char *fmt, ...)
{
    int available;
    int result;
    char *buffer;
    va_list valist;
    va_start(valist, fmt);
    available = MESSAGE_MAX_LENGTH - QuoteMessage->currentpos;
    buffer = QuoteMessage->buffer + QuoteMessage->currentpos;
    result = vsnprintf(buffer,available, fmt, valist);
    QuoteMessage->currentpos += result;
    QuoteMessage->length = QuoteMessage->currentpos;
    va_end(valist);
    return result;
}

static void ProcessPrimaryServiceSelect(Connection_t *connection, Message_t *message)
{
    char *serviceName;
    READSTRING(serviceName);
    Service_t *newservice = SetCurrentService(serviceName);
    if (newservice)
    {
        MessageRERR(message, RERR_OK, "");
    }
    else
    {
        MessageRERR(message, RERR_NOTFOUND, serviceName);
    }
    free(serviceName);
}

static void ProcessSecondaryServiceAdd(Connection_t *connection, Message_t *message)
{
    Output_t *output = NULL;
    char *serviceOutputName = NULL;
    char *destination = NULL;
    READ2STRINGS(serviceOutputName, destination);

    printlog(LOG_DEBUGV,"Add Service Output Name = \"%s\" Destination = \"%s\"\n",
        serviceOutputName, destination);

    output = OutputAllocate(serviceOutputName, OutputType_Service, destination);
    if (output)
    {
        MessageRERR(message, RERR_OK, "");
    }
    else
    {
        output = OutputFind(serviceOutputName, OutputType_Service);
        MessageRERR(message, output ? RERR_EXISTS:RERR_GENERIC, OutputErrorStr);
    }
    free(serviceOutputName);
    free(destination);
}

static void ProcessSecondaryServiceSet(Connection_t *connection, Message_t *message)
{
    Output_t *output = NULL;
    Service_t *newService = NULL;
    Service_t *oldService = NULL;
    char *serviceOutputName = NULL;
    char *serviceName = NULL;
    READ2STRINGS(serviceOutputName, serviceName);

    printlog(LOG_DEBUGV,"Set Service Output Name = \"%s\" Service = \"%s\"\n",
        serviceOutputName, serviceName);

    output = OutputFind(serviceOutputName, OutputType_Service);
    if (!output)
    {
        MessageRERR(message, RERR_NOTFOUND, serviceOutputName);
    }
    else
    {
        newService = ServiceFindName(serviceName);
        if (!newService)
        {
            MessageRERR(message, RERR_NOTFOUND, serviceName);
        }
        else
        {
            OutputGetService(output, &oldService);
            OutputSetService(output, newService);
            if (oldService)
            {
                ServiceFree(oldService);
            }
            MessageRERR(message, RERR_OK, "");
        }
    }
    free(serviceOutputName);
    free(serviceName);
}

static void ProcessSecondaryServiceRemove(Connection_t *connection, Message_t *message)
{
    Output_t *output = NULL;
    Service_t *oldService = NULL;
    char *serviceOutputName = NULL;
    READSTRING(serviceOutputName);

    if (strcmp(serviceOutputName, PrimaryService) == 0)
    {
        MessageRERR(message, RERR_GENERIC, "You cannot remove the primary service!");
        free(serviceOutputName);
        return;
    }

    printlog(LOG_DEBUGV,"Remove Service Output Name = \"%s\"\n",
        serviceOutputName);

    output = OutputFind(serviceOutputName, OutputType_Service);
    if (!output)
    {
        MessageRERR(message, RERR_NOTFOUND, serviceOutputName);
    }
    else
    {
        OutputGetService(output, &oldService);
        OutputFree(output);
        if (oldService)
        {
            ServiceFree(oldService);
        }
        MessageRERR(message, RERR_OK, "");
    }
    free(serviceOutputName);
}

static void ProcessServiceSetDestination(Connection_t *connection, Message_t *message)
{
    Output_t *output = NULL;
    char *serviceOutputName = NULL;
    char *mrl = NULL;
    READ2STRINGS(serviceOutputName, mrl);
    output = OutputFind(serviceOutputName, OutputType_Service);
    if (!output)
    {
        MessageRERR(message, RERR_NOTFOUND, serviceOutputName);
    }
    else
    {
        if (DeliveryMethodManagerFind(mrl, output->filter))
        {
            MessageRERR(message, RERR_OK, "");
        }
        else
        {
            MessageRERR(message, RERR_GENERIC, "No handler for specified MRL");
        }
    }
    free(serviceOutputName);
    free(mrl);
}

static void ProcessOutputAdd(Connection_t *connection, Message_t *message)
{
    Output_t *output = NULL;
    char *manualOutputName = NULL;
    char *destination = NULL;
    READ2STRINGS(manualOutputName, destination);

    printlog(LOG_DEBUGV,"Add Manual Output Name = \"%s\" Destination = \"%s\"\n",
        manualOutputName, destination);

    output = OutputAllocate(manualOutputName, OutputType_Manual, destination);
    if (output)
    {
        MessageRERR(message, RERR_OK, "");
    }
    else
    {
        output = OutputFind(manualOutputName, OutputType_Manual);
        MessageRERR(message, output ? RERR_EXISTS:RERR_GENERIC, OutputErrorStr);
    }
    free(manualOutputName);
    free(destination);
}

static void ProcessOutputRemove(Connection_t *connection, Message_t *message)
{
    Output_t *output = NULL;
    char *manualOutputName = NULL;
    READSTRING(manualOutputName);

    printlog(LOG_DEBUGV,"Remove Manual Output Name = \"%s\"\n",
        manualOutputName);

    output = OutputFind(manualOutputName, OutputType_Manual);
    if (!output)
    {
        MessageRERR(message, RERR_NOTFOUND, manualOutputName);
    }
    else
    {
        OutputFree(output);
        MessageRERR(message, RERR_OK, "");
    }
    free(manualOutputName);
}

static void ProcessOutputPIDAdd(Connection_t *connection, Message_t *message)
{
    Output_t *output = NULL;
    char *manualOutputName = NULL;

    READSTRING(manualOutputName);
    output = OutputFind(manualOutputName, OutputType_Manual);
    if (!output)
    {
        MessageRERR(message, RERR_NOTFOUND, manualOutputName);
        free(manualOutputName);
    }
    else
    {
        uint16_t i = 0;
        uint16_t pidCount = 0;
        uint16_t pid = 0;
        free(manualOutputName);
        READUINT16(pidCount);

        for (i = 0; i < pidCount; i ++)
        {
            READUINT16(pid);
            OutputAddPID(output, pid);
        }
        MessageRERR(message, RERR_OK, "");
    }
}

static void ProcessOutputPIDRemove(Connection_t *connection, Message_t *message)
{
    Output_t *output = NULL;
    char *manualOutputName = NULL;

    READSTRING(manualOutputName);
    output = OutputFind(manualOutputName, OutputType_Manual);
    if (!output)
    {
        MessageRERR(message, RERR_NOTFOUND, manualOutputName);
        free(manualOutputName);
    }
    else
    {
        uint16_t i = 0;
        uint16_t pidCount = 0;
        uint16_t pid = 0;
        free(manualOutputName);
        READUINT16(pidCount);

        for (i = 0; i < pidCount; i ++)
        {
            READUINT16(pid);
            OutputRemovePID(output, pid);
        }
        MessageRERR(message, RERR_OK, "");
    }
}

static void ProcessOutputSetDestination(Connection_t *connection, Message_t *message)
{
    Output_t *output = NULL;
    char *manualOutputName = NULL;
    char *mrl = NULL;
    READ2STRINGS(manualOutputName, mrl);
    output = OutputFind(manualOutputName, OutputType_Manual);
    if (!output)
    {
        MessageRERR(message, RERR_NOTFOUND, manualOutputName);
    }
    else
    {
        if (DeliveryMethodManagerFind(mrl, output->filter))
        {
            MessageRERR(message, RERR_OK, "");
        }
        else
        {
            MessageRERR(message, RERR_GENERIC, "No handler for specified MRL");
        }
    }
    free(manualOutputName);
    free(mrl);
}

static void ProcessPrimaryServiceCurrent(Connection_t *connection, Message_t *message)
{
    if (CurrentService)
    {
        MessageRERR(message, RERR_OK, CurrentService->name);
    }
    else
    {
        MessageRERR(message, RERR_GENERIC, "No service selected");
    }
}

static void ProcessSecondaryServiceList(Connection_t *connection, Message_t *message)
{
    ListIterator_t iterator;
    uint8_t outputsCount = 0;
    MessageInit(message, MSGCODE_RSSL);
    MessageWriteUint8(message, outputsCount);

    for ( ListIterator_Init(iterator, ServiceOutputsList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        Output_t *output = ListIterator_Current(iterator);
        Service_t *service = NULL;
        char *name = NULL;

        OutputGetService(output, &service);
        if (service)
        {
            name = service->name;
        }
        MessageEncode(message,"sss",
            output->name,
            DeliveryMethodGetMRL(output->filter),
            name);
        outputsCount ++;
    }
    MessageSeek(message, 0);
    MessageWriteUint8(message, outputsCount);
}

static void ProcessOutputsList(Connection_t *connection, Message_t *message)
{
    ListIterator_t iterator;
    uint8_t outputsCount = 0;

    MessageInit(message, MSGCODE_ROLO);
    MessageWriteUint8(message, outputsCount);

    for ( ListIterator_Init(iterator, ManualOutputsList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        Output_t *output = ListIterator_Current(iterator);
        MessageEncode(message, "ss",
            output->name,
            DeliveryMethodGetMRL(output->filter));
        outputsCount ++;
    }
    MessageSeek(message, 0);
    MessageWriteUint8(message, outputsCount);
}

static void ProcessOutputListPids(Connection_t *connection, Message_t *message)
{
    Output_t *output;
    char *outputName = NULL;

    if (MessageReadString(message, &outputName))
    {
        LOG_MALFORMED(connection, "output name");
        connection->connected = FALSE;
        return ;
    }
    output = OutputFind(outputName, OutputType_Manual);
    if (output)
    {
        int pidcount = 0, i;
        uint16_t *pids;
        OutputGetPIDs(output, &pidcount, &pids);
        MessageInit(message, MSGCODE_RLP);
        MessageWriteUint16(message, (uint8_t)pidcount);
        for (i = 0; i < pidcount; i ++)
        {
            MessageWriteUint16(message, pids[i]);
        }
    }
    else
    {
        MessageRERR(message, RERR_NOTFOUND, outputName);
    }
    free(outputName);
}

static void ProcessServiceFilterPacketCount(Connection_t *connection, Message_t *message)
{
    Output_t *output;
    char *outputName = NULL;

    if (MessageReadString(message, &outputName))
    {
        LOG_MALFORMED(connection, "output name");
        connection->connected = FALSE;
        return ;
    }
    output = OutputFind(outputName, OutputType_Service);
    if (output)
    {
        MessageInit(message, MSGCODE_ROPC);
        MessageWriteUint32(message, output->filter->packetsfiltered);
    }
    else
    {
        MessageRERR(message, RERR_NOTFOUND, outputName);
    }
    free(outputName);
}


static void ProcessOutputPacketCount(Connection_t *connection, Message_t *message)
{
    Output_t *output;
    char *outputName = NULL;

    if (MessageReadString(message, &outputName))
    {
        LOG_MALFORMED(connection, "output name");
        connection->connected = FALSE;
        return ;
    }
    output = OutputFind(outputName, OutputType_Manual);
    if (output)
    {
        MessageInit(message, MSGCODE_ROPC);
        MessageWriteUint32(message, output->filter->packetsfiltered);
    }
    else
    {
        MessageRERR(message, RERR_NOTFOUND, outputName);
    }
    free(outputName);
}

static void ProcessTSStats(Connection_t *connection, Message_t *message)
{
    MessageInit(message, MSGCODE_RTSS);
    MessageWriteUint32(message, TSFilter->bitrate);
    MessageWriteUint32(message, TSFilter->totalpackets);
    MessageWriteUint32(message, PIDFilters[PIDFilterIndex_PAT]->packetsprocessed);
    MessageWriteUint32(message, PIDFilters[PIDFilterIndex_PMT]->packetsprocessed);
    MessageWriteUint32(message, PIDFilters[PIDFilterIndex_SDT]->packetsprocessed);
}

static void ProcessFEStatus(Connection_t *connection, Message_t *message)
{
    fe_status_t status = 0;
    unsigned int ber = 0;
    unsigned int strength = 0;
    unsigned int snr = 0;

    DVBFrontEndStatus(DVBAdapter, &status, &ber, &strength, &snr);
    MessageInit(message, MSGCODE_RFES);
    MessageEncode(message, "bldd", status, ber, snr, strength);
}

static void ProcessServiceList(Connection_t *connection, Message_t *message, int all)
{
    uint16_t count = 0;
    ServiceEnumerator_t enumerator = NULL;
    MessageInit(message, MSGCODE_RLS);
    MessageWriteUint16(message, count);

    if (all)
    {
        enumerator = ServiceEnumeratorGet();
    }
    else
    {
        if (CurrentMultiplex != NULL)
        {
            enumerator = ServiceEnumeratorForMultiplex(CurrentMultiplex->freq);
        }

    }

    if (enumerator != NULL)
    {

        Service_t *service;
        do
        {
            service = ServiceGetNext(enumerator);
            if (service)
            {
                MessageWriteString(message, service->name);
                count ++;
                ServiceFree(service);
            }
        }
        while (service && !ExitProgram);

        ServiceEnumeratorDestroy(enumerator);
        MessageSeek(message, 0);
        MessageWriteUint16(message, count);
    }
}

static void ProcessServicePids(Connection_t *connection, Message_t *message)
{
    Service_t *service;
    char *serviceName = NULL;

    if (MessageReadString(message, &serviceName))
    {
        LOG_MALFORMED(connection, "sevice name");
        connection->connected = FALSE;
        return ;
    }
    service = ServiceFindName(serviceName);
    if (service)
    {
        int cached = 1;
        int i;
        int count = 0;
        PID_t *pids;
        pids = CachePIDsGet(service, &count);
        if (pids == NULL)
        {
            count = ServicePIDCount(service);
            cached = 0;
        }

        if ((count > 0) && (!cached))
        {
            pids = calloc(count, sizeof(PID_t));
            if (pids)
            {
                ServicePIDGet(service, pids, &count);
            }
            else
            {
                MessageRERR(message, RERR_GENERIC, "No memory to retrieve PIDs\n");
                return ;
            }
        }

        MessageInit(message, MSGCODE_RLP);
        MessageWriteUint16(message, (uint16_t)count);
        for (i = 0; i < count; i ++)
        {
            MessageWriteUint16(message, pids[i].pid);
        }

        if ((count > 0) && (!cached))
        {
            free(pids);
        }

        ServiceFree(service);
    }
    else
    {
        MessageRERR(message, RERR_NOTFOUND, serviceName);
    }
    free(serviceName);
}
