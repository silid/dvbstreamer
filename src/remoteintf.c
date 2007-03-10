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

#define _GNU_SOURCE
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

#define MAX_CONNECTIONS   2 /* 1 for monitoring by web and another for control */
#define MAX_LINE_LENGTH 256


typedef struct Connection_t
{
    int socketfd;
    FILE *socketfp;

    struct sockaddr_in clientAddress;
    bool authenticated;
    bool active;
    bool connected;
    pthread_t thread;
}
Connection_t;


static void HandleConnection(Connection_t *connection);
static void RemoteInterfaceAuthenticate(int argc, char **argv);
static void RemoteInterfaceWho(int argc, char **argv);
static void RemoteInterfaceLogout(int argc, char **argv);
static void RemoteInterfaceInfo(int argc, char **argv);
static bool PrintResponse(FILE *fp, uint16_t errno, char * msg);
static int  RemoteInterfacePrintfImpl(char *format, ...);

static Command_t RemoteInterfaceCommands[] = {
        {
            "who",
            FALSE, 0, 0,
            "Display current control connections.",
            "List all the control connections and if they are authenticated.",
            RemoteInterfaceWho
        },
        {
            "info",
            TRUE, 1, 1,
            "Display information about the server.",
            "info <property>\n"
            "Display information about the server.\n"
            "Available properties are:\n"
            "\tname   : Server name.\n"
            "\tuptime : Uptime of the server.\n"
            "\tupsecs : Number of seconds the server has been up.\n",
            RemoteInterfaceInfo
        },
        {NULL, FALSE, 0, 0, NULL, NULL}
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

static int activeConnections = 0;
static pthread_mutex_t activeConnectionsMutex = PTHREAD_MUTEX_INITIALIZER;
static int serverSocket;
static Connection_t connections[MAX_CONNECTIONS];
static char *infoStreamerName;
static char *authUsername;
static char *authPassword;

static pthread_t acceptThread;

static time_t serverStartTime;
static char responselineStart[] = "DVBStreamer/" VERSION "/";

int RemoteInterfaceInit(int adapter, char *streamerName, char *bindAddress, char *username, char *password)
{
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
        printlog(LOG_DEBUG, "Failed to set bind address\n");
        return 1;
    }

    if (addrinfo->ai_addrlen > sizeof(struct sockaddr_storage))
    {
        freeaddrinfo(addrinfo);
	printlog(LOG_DEBUG, "Failed to parse bind address\n");
        return 1;
    }
    address_len = addrinfo->ai_addrlen;
    memcpy(&address, addrinfo->ai_addr, addrinfo->ai_addrlen);
    freeaddrinfo(addrinfo);

    serverSocket = socket(address.ss_family, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket < 0)
    {
        printlog(LOG_ERROR, "Failed to create server socket!\n");
        return 1;
    }
    if (bind(serverSocket, (struct sockaddr *) &address, address_len) < 0)
    {
        printlog(LOG_ERROR, "Failed to bind server to port %d\n", REMOTEINTERFACE_PORT + adapter);
        close(serverSocket);
        return 1;
    }
#else
    struct sockaddr_in serverAddress;

    serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket < 0)
    {
        printlog(LOG_ERROR, "Failed to create server socket!\n");
        return 1;
    }

    memset((void *) &serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(REMOTEINTERFACE_PORT + adapter);

    if (bind(serverSocket, (struct sockaddr *) &serverAddress, sizeof(serverAddress)) < 0)
    {
        printlog(LOG_ERROR, "Failed to bind server to port %d\n", REMOTEINTERFACE_PORT + adapter);
        close(serverSocket);
        return 1;
    }
#endif

    listen(serverSocket, 1);

    infoStreamerName = strdup(streamerName);
    authUsername = strdup(username);
    authPassword = strdup(password);

    time(&serverStartTime);
    printlog(LOG_INFO, "Server created %s", ctime(&serverStartTime));
    printlog(LOG_DEBUG, "Username    : %s\n", authUsername);
    printlog(LOG_DEBUG, "Password    : %s\n", authPassword);
    printlog(LOG_DEBUG, "Server Name : %s\n", infoStreamerName);

    CommandRegisterCommands(RemoteInterfaceCommands);
    return 0;
}

void RemoteInterfaceDeInit(void)
{
    int i;
    CommandUnRegisterCommands(RemoteInterfaceCommands);

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
    struct pollfd pfd[1];

    pfd[0].fd = serverSocket;
    pfd[0].events = POLLIN;

    while (!ExitProgram)
    {
        if (poll(pfd, 1, 200))
        {
            if (pfd[0].revents & POLLIN)
            {
                FILE *clientfp;
                int clientfd;
                struct sockaddr_in clientAddress;
                int clientAddressSize;

                clientAddressSize = sizeof(clientAddress);
                clientfd = accept(serverSocket, (struct sockaddr *) & clientAddress, &clientAddressSize);
                if (clientfd < 0)
                {
                    continue;
                }
                clientfp = fdopen(clientfd, "r+");

                pthread_mutex_lock(&activeConnectionsMutex);
                if (activeConnections >= MAX_CONNECTIONS)
                {
                    printlog(LOG_INFO, "Connection attempt from %s:%d rejected as too many open connections!\n",
                             inet_ntoa(clientAddress.sin_addr), clientAddress.sin_port);
                    PrintResponse(clientfp,COMMAND_ERROR_TOO_MANY_CONNS, "Too many connect clients!");
                    fclose(clientfp);
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
                        connections[i].socketfp = clientfp;
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
                        PrintResponse(clientfp,COMMAND_ERROR_TOO_MANY_CONNS, "Too many connect clients!");
                        fclose(clientfp);
                    }

                }
                pthread_mutex_unlock(&activeConnectionsMutex);
            }
        }

    }
}

/******************************************************************************
* Connection functions                                                        *
******************************************************************************/
static void HandleConnection(Connection_t *connection)
{
    struct pollfd pfd[1];
    char line[MAX_LINE_LENGTH];
    int socketfd = connection->socketfd;
    FILE *socketfp = connection->socketfp;
    CommandContext_t context;
    pfd[0].fd = socketfd;
    pfd[0].events = POLLIN;


    /* Setup context */
    asprintf(&context.interface, "%s:%d",
        inet_ntoa(connection->clientAddress.sin_addr), connection->clientAddress.sin_port);
    context.authenticated = FALSE;
    context.remote = TRUE;
    context.privateArg = connection;
    context.commands = ConnectionCommands;

    PrintResponse(socketfp,COMMAND_OK, "Ready");

    while (!ExitProgram && connection->connected)
    {
        int r;
        pfd[0].revents = 0;
        r = poll(pfd, 1, 30000);
        if (pfd[0].revents & POLLIN)
        {
            char *nl;
            // Read in the command
            if (fgets(line, MAX_LINE_LENGTH, socketfp))
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
                printlog(LOG_DEBUG, "%s: Received Line: \"%s\"\n", context.interface, line);
                CommandExecute(&context, RemoteInterfacePrintfImpl, line);
                PrintResponse(socketfp,context.errorNumber, context.errorMessage);
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
    printlog(LOG_INFO, "%s: Connection closed!\n", context.interface);
    /* Close the socket and free our resources */
    fclose(socketfp);
    connection->connected = FALSE;
    free(context.interface);

    pthread_mutex_lock(&activeConnectionsMutex);
    connection->active = FALSE;
    activeConnections --;
    pthread_mutex_unlock(&activeConnectionsMutex);
}

static bool PrintResponse(FILE *fp, uint16_t errno, char * msg)
{
    fprintf(fp, "%s%d %s\n", responselineStart, errno, msg);
    fflush(fp);
}

static int RemoteInterfacePrintfImpl(char *format, ...)
{
    int result = 0;
    Connection_t *connection = (Connection_t*)CurrentCommandContext->privateArg;
    va_list valist;

    va_start(valist, format);
    result = vfprintf(connection->socketfp, format, valist);
    va_end(valist);
    return result;
}

/******************************************************************************
* Commands                                                                    *
******************************************************************************/
static void RemoteInterfaceAuthenticate(int argc, char **argv)
{
    if ((strcmp(argv[0], authUsername) == 0) &&
        (strcmp(argv[1], authPassword) == 0))
    {
        CurrentCommandContext->authenticated = TRUE;
        CommandError(COMMAND_OK, "Authenticated.");
    }
    else
    {
        CurrentCommandContext->authenticated = FALSE;
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
    if (CurrentCommandContext->remote)
    {
        Connection_t *connection = (Connection_t*)CurrentCommandContext->privateArg;
        connection->connected = FALSE;
        CommandError(COMMAND_OK, "Bye!");
    }
    else
    {
        CommandError(COMMAND_ERROR_GENERIC, "Not a remote connection!");
    }
}
static void RemoteInterfaceInfo(int argc, char **argv)
{
    if (strcmp("name", argv[0]) == 0)
    {
        CommandPrintf("%s\n", infoStreamerName);
    }
    else if (strcmp("uptime", argv[0]) == 0)
    {
        time_t now;
        int seconds;
        int d, h, m, s;
        time(&now);
        seconds = (int)difftime(now, serverStartTime);
        d = seconds / (24 * 60 * 60);
        h = (seconds - (d * 24 * 60 * 60)) / (60 * 60);
        m = (seconds - ((d * 24 * 60 * 60) + (h * 60 * 60))) / 60;
        s = (seconds - ((d * 24 * 60 * 60) + (h * 60 * 60) + (m * 60)));
        CommandPrintf("%d Days %d Hours %d Minutes %d seconds\n", d, h, m, s);
    }
    else if (strcmp("upsecs", argv[0]) == 0)
    {
        time_t now;
        time(&now);

        CommandPrintf("%d\n", (int)difftime(now, serverStartTime));
    }
    else
    {
        CommandError(COMMAND_ERROR_GENERIC, "Unknown property");
    }
}
