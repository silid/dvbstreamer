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
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "logging.h"
#include "cache.h"
#include "messages.h"
#include "main.h"

#define PORT 54197 // 0xd3b5 ~= DVBS

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
          MessageRERR(RERR_NOTAUTHORISED, "Not authorised!"); \
        }\
    }while(0)

typedef struct Connection_t
{
    int socketfd;
    struct sockaddr_in clientAddress;
    int authenticated;
    int active;
    int connected;
    pthread_t thread;
    Message_t message;
}Connection_t;

static void HandleConnection(Connection_t *connection);
static void ProcessMessage(Connection_t *connection, Message_t *message);
static void ProcessInfo(Connection_t *connection, Message_t *message);
static void ProcessAuth(Connection_t *connection, Message_t *message);
static void ProcessServiceList(Connection_t *connection, Message_t *message, int all);
static void ProcessServicePids(Connection_t *connection, Message_t *message);

static int activeConnections = 0;
static pthread_mutex_t activeConnectionsMutex = PTHREAD_MUTEX_INITIALIZER;
static int serverSocket;
static Connection_t connections[MAX_CONNECTIONS];
static char *infoStreamerName;
static char *authUsername;
static char *authPassword;

static time_t serverStartTime;

int BinaryCommsInit(int adapter, char *streamername, char *username, char *password)
{
    struct sockaddr_in serverAddress;
    serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket < 0) 
    {
        printlog(LOG_ERROR, "Failed to create server socket!");
        return 1;
    }

    bzero((char *) &serverAddress, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(PORT + adapter);
    if (bind(serverSocket, (struct sockaddr *) &serverAddress, sizeof(serverAddress)) < 0)
    {
        printlog(LOG_ERROR, "Failed to bind server to port %d", PORT + adapter);
        close(serverSocket);
        return 1;
    }
    listen(serverSocket,1);
    
    infoStreamerName = strdup(streamername);
    authUsername = strdup(username);
    authPassword = strdup(password);
    
    time(&serverStartTime);
    printlog(LOG_INFO, "Server created %s\n", ctime(&serverStartTime));
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
            // Force closure of the sockets should cause the thread to terminate.
            close(connections[i].socketfd);
            connections[i].connected = 0;
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
        if (poll(pfd,1, 200))
        {
            if (pfd[0].revents & POLLIN)
            {
                int clientfd;
                struct sockaddr_in clientAddress;
                int clientAddressSize;
                
                clientAddressSize = sizeof(clientAddress);
                clientfd = accept(serverSocket, (struct sockaddr *) &clientAddress, &clientAddressSize);
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
                        connections[i].active = 1;
                        connections[i].connected = 1;
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
    int socketfd = connection->socketfd;
    
    Message_t *message = &connection->message;
    
    while(!ExitProgram && connection->connected)
    {
        if (MessageRecv(message, socketfd))
        {
            /* Socket must be dead exit this connection! */
            break;
        }
        ProcessMessage(connection, message);
        if (connection->connected)
        {
            printlog(LOG_DEBUG, "%s:%d : Response 0x%02x length %d\n",
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
    connection->connected = 0;
    
    pthread_mutex_lock(&activeConnectionsMutex);
    connection->active = 0;
    activeConnections --;
    pthread_mutex_unlock(&activeConnectionsMutex);
}

static void ProcessMessage(Connection_t *connection, Message_t *message)
{
    printlog(LOG_DEBUG, "%s:%d : Processing message 0x%02x length %d\n",
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
        case MSGCODE_CSPS:
        case MSGCODE_CSSA:
        case MSGCODE_CSSS:
        case MSGCODE_CSSR:
        case MSGCODE_COAO:
        case MSGCODE_CORO:
        case MSGCODE_COAP:
        case MSGCODE_CORP:
        case MSGCODE_SSPC:
        case MSGCODE_SSSL:
        case MSGCODE_SOLO:
        case MSGCODE_SOLP:
        case MSGCODE_SOPC:
        case MSGCODE_STSS:
        case MSGCODE_SFES:
            MessageRERR(message, RERR_UNDEFINED, "Not implemented!");
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
            MessageRERR(message, RERR_UNDEFINED, "Unknown message type!");
            break;
    }
}

static void ProcessInfo(Connection_t *connection, Message_t *message)
{
    uint8_t field;
    if (MessageReadUint8(message, &field))
    {
        LOG_MALFORMED(connection, "info");
        connection->connected = 0;
    }
    
    switch (field)
    {
        case INFO_NAME:
            MessageRERR(message, RERR_OK, infoStreamerName);
            break;
        
        case INFO_FETYPE:
            MessageRERR(message, RERR_OK, "Not implemented!");
            break;
        
        case INFO_AUTHENTICATED:
            MessageRERR(message, RERR_OK, connection->authenticated ? "Authenticated":"Not authenticated");
            break;
        
        case INFO_UPTIMESEC:
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
                int d,h,m,s;
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
            MessageRERR(message, RERR_UNDEFINED, "Unknown field");
            break;
    }
}

static void ProcessAuth(Connection_t *connection, Message_t *message)
{
    char *msgUsername = NULL;
    char *msgPassword = NULL;

    if (MessageReadString(message, &msgUsername))
    {
        LOG_MALFORMED(connection, "username");
        connection->connected = 0;
        return;
    }
    
    if (MessageReadString(message, &msgPassword))
    {
        LOG_MALFORMED(connection, "password");
        connection->connected = 0;
        free(msgUsername);
        return;
    }
    connection->authenticated = (strcmp(msgUsername, authUsername) == 0) && 
                                (strcmp(msgPassword, authPassword) == 0);
                                
    MessageReset(message);
    MessageRERR(message, connection->authenticated ? RERR_OK:RERR_NOTAUTHORISED, NULL);
    free(msgUsername);
    free(msgPassword);
}

static void ProcessServiceList(Connection_t *connection, Message_t *message, int all)
{
    uint16_t count  = 0;
    ServiceEnumerator_t enumerator = NULL;
    MessageReset(message);
    MessageSetCode(message,MSGCODE_RSL);
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
        while(service && !ExitProgram);
        
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
        connection->connected = 0;
        return;
    }
    service = ServiceFindName(serviceName);
    if (service)
    {
        int cached = 1;
        int i;
        int count;
        PID_t *pids;
        pids = CachePIDsGet(service, &count);
        if (pids == NULL)
        {
            count = ServicePIDCount(service);
            cached = 0;
        }
        if (count > 0)
        {
            if (!cached)
            {
                pids = calloc(count, sizeof(PID_t));
                if (pids)
                {
                    ServicePIDGet(service, pids, &count);
                }
                else
                {
                    MessageRERR(message, RERR_UNDEFINED, "No memory to retrieve PIDs\n");
                    return;
                }
            }
            MessageReset(message);
            MessageSetCode(message, MSGCODE_RLP);
            MessageWriteUint8(message, (uint8_t)count);
            for (i = 0; i < count; i ++)
            {
                MessageWriteUint16(message, pids[i].pid);
            }
            
            if (!cached)
            {
                free(pids);
            }
        }
        ServiceFree(service);
    }
    else
    {
        MessageRERR(message, RERR_NOTFOUND, serviceName);
    }
}
