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

remoteintf.c

Remote Interface functions.

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
#include <netdb.h>

#include "main.h"
#include "logging.h"
#include "commands.h"
#include "remoteintf.h"

/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/
#define MAX_CONNECTIONS   2 /* 1 for monitoring by web and another for control */
#define MAX_LINE_LENGTH 256


/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/
typedef struct Connection_t
{
    int socketfd;
    FILE *readfp;
    FILE *writefp;

    struct sockaddr_in clientAddress;
    bool authenticated;
    bool active;
    bool connected;
    pthread_t thread;
}
Connection_t;


/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void HandleConnection(Connection_t *connection);
static void RemoteInterfaceAuthenticate(int argc, char **argv);
static void RemoteInterfaceWho(int argc, char **argv);
static void RemoteInterfaceLogout(int argc, char **argv);
static void RemoteInterfaceServerNameGet(char *name);
static void RemoteInterfaceServerNameSet(char *name, int argc, char **argcv);
static void PrintResponse(FILE *fp, uint16_t errno, char * msg);
static int RemoteInterfacePrintfImpl(CommandContext_t *context, const char *format, va_list args);
static char *RemoteInterfaceGetsImpl(CommandContext_t *context, char *buffer, int len);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static Command_t RemoteInterfaceCommands[] = {
        {
            "who",
            FALSE, 0, 0,
            "Display current control connections.",
            "List all the control connections and if they are authenticated.",
            RemoteInterfaceWho
        },
        {NULL, FALSE, 0, 0, NULL, NULL}
    };

static CommandVariable_t CommandVariableServerName = {
    "name",
    "Server Name",
    RemoteInterfaceServerNameGet,
    RemoteInterfaceServerNameSet
};

static Command_t ConnectionCommands[] = {
    {
        "auth",
        TRUE, 2, 2,
        "Login to control dvbstreamer.",
        "auth <username> <password>\n"
                "Authenticate as the user that is able to select channels etc.",
        RemoteInterfaceAuthenticate
    },
    {
        "logout",
        FALSE, 0, 0,
        "Close the current control connection.",
        "Close the current control connection (only works for remote connections).",
        RemoteInterfaceLogout
    },
    {NULL, FALSE, 0, 0, NULL, NULL}
};

static bool remoteIntfExit = FALSE;

static pthread_t acceptThread;
static pthread_mutex_t connectionsMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t connectionCondVar = PTHREAD_COND_INITIALIZER;

static int serverSocket;
static Connection_t connections[MAX_CONNECTIONS];

static char *infoStreamerName;
static char *authUsername;
static char *authPassword;


static time_t serverStartTime;
static char responselineStart[] = "DVBStreamer/" VERSION "/";

static char REMOTEINTERFACE[] = "RemoteInterface";

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
int RemoteInterfaceInit(int adapter, char *streamerName, char *bindAddress, char *username, char *password)
{
     int i;
#ifndef __CYGWIN__
    socklen_t address_len;
    struct sockaddr_storage address;
    struct addrinfo *addrinfo, hints;
    char portnumber[10];

    sprintf(portnumber, "%d", REMOTEINTERFACE_PORT + adapter);
    
    memset((void *)&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_ADDRCONFIG | AI_PASSIVE;
    if ((getaddrinfo(bindAddress, portnumber, &hints, &addrinfo) != 0) || (addrinfo == NULL))
    {
        LogModule(LOG_DEBUG, REMOTEINTERFACE, "Failed to set bind address\n");
        return 1;
    }

    if (addrinfo->ai_addrlen > sizeof(struct sockaddr_storage))
    {
        freeaddrinfo(addrinfo);
        LogModule(LOG_DEBUG, REMOTEINTERFACE, "Failed to parse bind address\n");
        return 1;
    }
    address_len = addrinfo->ai_addrlen;
    memcpy(&address, addrinfo->ai_addr, addrinfo->ai_addrlen);
    freeaddrinfo(addrinfo);

    serverSocket = socket(address.ss_family, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket < 0)
    {
        LogModule(LOG_ERROR, REMOTEINTERFACE, "Failed to create server socket!\n");
        return 1;
    }
    if (bind(serverSocket, (struct sockaddr *) &address, address_len) < 0)
    {
        LogModule(LOG_ERROR, REMOTEINTERFACE, "Failed to bind server to port %d\n", REMOTEINTERFACE_PORT + adapter);
        close(serverSocket);
        return 1;
    }
#else
    struct sockaddr_in serverAddress;

    serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket < 0)
    {
        LogModule(LOG_ERROR, REMOTEINTERFACE, "Failed to create server socket!\n");
        return 1;
    }

    memset((void *) &serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(REMOTEINTERFACE_PORT + adapter);

    if (bind(serverSocket, (struct sockaddr *) &serverAddress, sizeof(serverAddress)) < 0)
    {
        LogModule(LOG_ERROR, REMOTEINTERFACE, "Failed to bind server to port %d\n", REMOTEINTERFACE_PORT + adapter);
        close(serverSocket);
        return 1;
    }
#endif

    listen(serverSocket, 1);

    infoStreamerName = strdup(streamerName);
    authUsername = strdup(username);
    authPassword = strdup(password);

    time(&serverStartTime);
    LogModule(LOG_INFO, REMOTEINTERFACE, "Server created %s", ctime(&serverStartTime));
    LogModule(LOG_DEBUG, REMOTEINTERFACE, "Username    : %s\n", authUsername);
    LogModule(LOG_DEBUG, REMOTEINTERFACE, "Password    : %s\n", authPassword);
    LogModule(LOG_DEBUG, REMOTEINTERFACE, "Server Name : %s\n", infoStreamerName);

    for (i = 0; i < MAX_CONNECTIONS; i ++)
    {
        connections[i].active = FALSE;
        pthread_create(&connections[i].thread, NULL, (void*)HandleConnection, (void*)&connections[i]);
    }
    
    CommandRegisterCommands(RemoteInterfaceCommands);
    CommandRegisterVariable(&CommandVariableServerName);
    return 0;
}

void RemoteInterfaceDeInit(void)
{
    int i;
    CommandUnRegisterCommands(RemoteInterfaceCommands);
    CommandUnRegisterVariable(&CommandVariableServerName);

    remoteIntfExit = TRUE;
    pthread_cond_broadcast(&connectionCondVar);
    close(serverSocket);
    for ( i = 0; i < MAX_CONNECTIONS; i ++)
    {
        if (connections[i].connected)
        {
            /* Force closure of the sockets should cause the thread to terminate. */
            close(connections[i].socketfd);
            connections[i].connected = FALSE;
        }
        pthread_join(connections[i].thread, NULL);
    }
    free(infoStreamerName);
    free(authUsername);
    free(authPassword);
}

void RemoteInterfaceAsyncAcceptConnections(void)
{
    pthread_create(&acceptThread, NULL, (void*)RemoteInterfaceAcceptConnections, NULL);
}

void RemoteInterfaceAcceptConnections(void)
{
    int i;
    int found;
    struct pollfd pfd[1];

    pfd[0].fd = serverSocket;
    pfd[0].events = POLLIN;

    while (!remoteIntfExit && !ExitProgram)
    {
        if (poll(pfd, 1, 200))
        {
            if (pfd[0].revents & POLLIN)
            {
                FILE *readfp;
                FILE *writefp;
                int clientfd;
                struct sockaddr_in clientAddress;
                socklen_t clientAddressSize;

                clientAddressSize = sizeof(clientAddress);
                clientfd = accept(serverSocket, (struct sockaddr *) & clientAddress, &clientAddressSize);
                if (clientfd < 0)
                {
                    continue;
                }
                readfp = fdopen(clientfd, "r");
                writefp =fdopen(clientfd, "w"); 
                pthread_mutex_lock(&connectionsMutex);
                found = -1;

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
                    connections[i].readfp = readfp;
                    connections[i].writefp = writefp;
                    connections[i].clientAddress = clientAddress;
                    LogModule(LOG_INFO, REMOTEINTERFACE, "Connection attempt from %s:%d accepted!\n",
                             inet_ntoa(clientAddress.sin_addr), clientAddress.sin_port);
                    /* Wake up the connection threads */
                    pthread_cond_broadcast(&connectionCondVar);
                }
                else
                {
                    LogModule(LOG_INFO, REMOTEINTERFACE, "Connection attempt from %s:%d rejected as no connections structures left!\n",
                             inet_ntoa(clientAddress.sin_addr), clientAddress.sin_port);
                    PrintResponse(writefp,COMMAND_ERROR_TOO_MANY_CONNS, "Too many connect clients!");
                    fclose(readfp);
                    fclose(writefp);
                }

                pthread_mutex_unlock(&connectionsMutex);
            }
        }
    }
    LogModule(LOG_DEBUG,REMOTEINTERFACE,"Accept thread exiting.\n");
}

/******************************************************************************
* Connection functions                                                        *
******************************************************************************/
static void HandleConnection(Connection_t *connection)
{
    struct pollfd pfd[1];
    char line[MAX_LINE_LENGTH];
    int socketfd;
    FILE *readfp;
    FILE *writefp;
    CommandContext_t context;


    while(!remoteIntfExit)
    {
        LogModule(LOG_DEBUG, REMOTEINTERFACE, "Waiting for connection (%p)\n", connection);

        pthread_mutex_lock(&connectionsMutex);
        connection->active = FALSE;
        pthread_cond_wait(&connectionCondVar, &connectionsMutex);
        pthread_mutex_unlock(&connectionsMutex);
        
        if (!connection->active)
        {
            continue;
        }

        LogModule(LOG_DEBUG, REMOTEINTERFACE, "Connection accepted (%p)\n", connection);
        /* Setup context */
        asprintf(&context.interface, "%s:%d",
            inet_ntoa(connection->clientAddress.sin_addr), connection->clientAddress.sin_port);
        context.authenticated = FALSE;
        context.remote = TRUE;
        context.printf = RemoteInterfacePrintfImpl;
        context.gets = RemoteInterfaceGetsImpl;
        context.privateArg = connection;
        context.commands = ConnectionCommands;

        socketfd = connection->socketfd;
        readfp = connection->readfp;
        writefp = connection->writefp;
        pfd[0].fd = socketfd;
        pfd[0].events = POLLIN;
    
        PrintResponse(writefp,COMMAND_OK, "Ready");

        while (!remoteIntfExit && connection->connected)
        {
            int r;
            pfd[0].revents = 0;
            r = poll(pfd, 1, 30000);
            if (pfd[0].revents & POLLIN)
            {
                char *nl;
                // Read in the command
                if (fgets(line, MAX_LINE_LENGTH, readfp))
                {
                    nl = strchr(line, '\n');
                    if (nl)
                    {
                        *nl = 0;
                    }
                    nl = strchr(line, '\r');
                    if (nl)
                    {
                        *nl = 0;
                    }
                    LogModule(LOG_DEBUG, REMOTEINTERFACE, "%s: Received Line: \"%s\"\n", context.interface, line);
                    CommandExecute(&context, line);
                    PrintResponse(writefp,context.errorNumber, context.errorMessage);
                }
                else
                {
                    connection->connected = FALSE;
                }
            }
            else
            {
                connection->connected = FALSE;
            }
        }
        LogModule(LOG_INFO, REMOTEINTERFACE, "%s: Connection closed!\n", context.interface);
        /* Close the socket and free our resources */
        fclose(readfp);
        fclose(writefp);        
        connection->connected = FALSE;
        free(context.interface);
    }
    LogModule(LOG_DEBUG,REMOTEINTERFACE,"Connection thread exiting.\n");
}

static void PrintResponse(FILE *fp, uint16_t errno, char * msg)
{
    fprintf(fp, "%s%d %s\n", responselineStart, errno, msg);
    fflush(fp);
}

static int RemoteInterfacePrintfImpl(CommandContext_t *context, const char *format, va_list args)
{
    Connection_t *connection = (Connection_t*)context->privateArg;
    return vfprintf(connection->writefp, format, args);
}

static char *RemoteInterfaceGetsImpl(CommandContext_t *context, char *buffer, int len)
{
    Connection_t *connection = (Connection_t*)context->privateArg;
    return fgets(buffer, len, connection->readfp);
}

/******************************************************************************
* Commands                                                                    *
******************************************************************************/
static void RemoteInterfaceAuthenticate(int argc, char **argv)
{
    CommandContext_t *context = CommandContextGet();
    if ((strcmp(argv[0], authUsername) == 0) &&
        (strcmp(argv[1], authPassword) == 0))
    {
        context->authenticated = TRUE;
        CommandError(COMMAND_OK, "Authenticated.");
    }
    else
    {
        context->authenticated = FALSE;
        CommandError(COMMAND_ERROR_AUTHENTICATION, "Authentication failed!");
    }
}

static void RemoteInterfaceWho(int argc, char **argv)
{
    int i;
    for (i = 0; i < MAX_CONNECTIONS; i ++)
    {
        if (connections[i].connected)
        {
            CommandPrintf("%s:%d %s\n", inet_ntoa(connections[i].clientAddress.sin_addr), connections[i].clientAddress.sin_port,
            connections[i].authenticated ? "(Authentitcated)":"");
        }
    }
}
static void RemoteInterfaceLogout(int argc, char **argv)
{
    CommandContext_t *context = CommandContextGet();    
    if (context->remote)
    {
        Connection_t *connection = (Connection_t*)context->privateArg;
        connection->connected = FALSE;
        CommandError(COMMAND_OK, "Bye!");
    }
    else
    {
        CommandError(COMMAND_ERROR_GENERIC, "Not a remote connection!");
    }
}

static void RemoteInterfaceServerNameGet(char *name)
{
    CommandPrintf("%s\n", infoStreamerName);
}

static void RemoteInterfaceServerNameSet(char *name, int argc, char **argv)
{
    free(infoStreamerName);
    infoStreamerName = strdup(argv[0]);
    CommandPrintf("%s\n", infoStreamerName);
}

