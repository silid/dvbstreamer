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

#include "properties.h"

/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/
#define MAX_CONNECTIONS   2 /* 1 for monitoring by web and another for control */
#define MAX_LINE_LENGTH 256

/* Max connection string = [xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx]:xxxxx */
#define MAX_CONNECTION_STR_LENGTH 48

/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/
typedef struct Connection_t
{
    int socketfd;
    FILE *fp;

    struct sockaddr_storage clientAddress;
    bool connected;
    pthread_t thread;
}
Connection_t;


/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void AddConnection(int socketfd, struct sockaddr_storage *clientAddress);
static void RemoveConnection(Connection_t *connection);
static void HandleConnection(Connection_t *connection);
static void GetConnectionString(struct sockaddr_storage *connAddr, char *output);

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

static List_t *connectionsList;

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
#ifdef USE_GETADDRINFO
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

    ObjectRegisterType(Connection_t);
    connectionsList = ListCreate();

    listen(serverSocket, 1);

    infoStreamerName = strdup(streamerName);
    authUsername = strdup(username);
    authPassword = strdup(password);

    time(&serverStartTime);
    LogModule(LOG_INFO, REMOTEINTERFACE, "Server created %s", ctime(&serverStartTime));
    LogModule(LOG_DEBUG, REMOTEINTERFACE, "Username    : %s\n", authUsername);
    LogModule(LOG_DEBUG, REMOTEINTERFACE, "Password    : %s\n", authPassword);
    LogModule(LOG_DEBUG, REMOTEINTERFACE, "Server Name : %s\n", infoStreamerName);

    CommandRegisterCommands(RemoteInterfaceCommands);
    CommandRegisterVariable(&CommandVariableServerName);
    PropertiesAddProperty("sys.rc", "servername", "Name of this dvbstreamer instance.",
                          PropertyType_String, &infoStreamerName,
                          PropertiesSimplePropertyGet, NULL);

    PropertiesAddProperty("sys.rc", "username", "Username used to authenticate.",
                          PropertyType_String, &infoStreamerName,
                          NULL, PropertiesSimplePropertySet);
    PropertiesAddProperty("sys.rc", "password", "Password used to authenticate.",
                          PropertyType_String, &infoStreamerName,
                          NULL, PropertiesSimplePropertySet);
    return 0;
}

void RemoteInterfaceDeInit(void)
{
    ListIterator_t iterator;

    CommandUnRegisterCommands(RemoteInterfaceCommands);
    CommandUnRegisterVariable(&CommandVariableServerName);

    remoteIntfExit = TRUE;
    close(serverSocket);

    pthread_mutex_lock(&connectionsMutex);
    if (ListCount(connectionsList) > 0)
    {
        for (ListIterator_Init(iterator, connectionsList);
             ListIterator_MoreEntries(iterator);
             ListIterator_Next(iterator))
        {
            Connection_t *connection = (Connection_t*)ListIterator_Current(iterator);
            close(connection->socketfd);
        }

        pthread_cond_wait(&connectionCondVar, &connectionsMutex);
    }
    pthread_mutex_unlock(&connectionsMutex);

    free(infoStreamerName);
    free(authUsername);
    free(authPassword);
    ListFree(connectionsList, NULL);
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

    while (!remoteIntfExit && !ExitProgram)
    {
        if (poll(pfd, 1, 200))
        {
            if (pfd[0].revents & POLLIN)
            {
                int clientfd;
                struct sockaddr_storage clientAddress;
                socklen_t clientAddressSize;

                clientAddressSize = sizeof(clientAddress);
                clientfd = accept(serverSocket, (struct sockaddr *) &clientAddress, &clientAddressSize);
                if (clientfd < 0)
                {
                    continue;
                }
                AddConnection(clientfd, &clientAddress);
            }
        }
    }
    LogModule(LOG_DEBUG,REMOTEINTERFACE,"Accept thread exiting.\n");
}

static void AddConnection(int socketfd, struct sockaddr_storage *clientAddress)
{
    char connectionStr[MAX_CONNECTION_STR_LENGTH];
    FILE *fp;
    Connection_t *connection = ObjectCreateType(Connection_t);

    GetConnectionString(clientAddress, connectionStr);

    fp = fdopen(socketfd, "r+");

    if (connection)
    {
        connection->connected = TRUE;
        connection->socketfd = socketfd;
        connection->fp = fp;
        connection->clientAddress = *clientAddress;
        LogModule(LOG_INFO, REMOTEINTERFACE, "Connection attempt from %s accepted!\n",
                 connectionStr);

        pthread_mutex_lock(&connectionsMutex);
        ListAdd(connectionsList, connection);
        pthread_mutex_unlock(&connectionsMutex);

        pthread_create(&connection->thread, NULL, (void*)HandleConnection, (void*)connection);
    }
    else
    {
        LogModule(LOG_INFO, REMOTEINTERFACE, "Connection attempt from %s rejected as no connections structures left!\n",
                 connectionStr);

        PrintResponse(fp, COMMAND_ERROR_TOO_MANY_CONNS, "Too many connect clients!");
        fclose(fp);
    }
}

static void RemoveConnection(Connection_t *connection)
{
    pthread_mutex_lock(&connectionsMutex);
    ListRemove(connectionsList, connection);
    ObjectRefDec(connection);
    if ((ListCount(connectionsList) == 0) && remoteIntfExit)
    {
        pthread_cond_broadcast(&connectionCondVar);
    }
    pthread_mutex_unlock(&connectionsMutex);
}
/******************************************************************************
* Connection functions                                                        *
******************************************************************************/
static void HandleConnection(Connection_t *connection)
{
    struct pollfd pfd[1];
    char connectionStr[MAX_CONNECTION_STR_LENGTH];
    char line[MAX_LINE_LENGTH];
    int socketfd;
    FILE *fp;
    CommandContext_t context;

    /* Setup context */
    GetConnectionString(&connection->clientAddress, connectionStr);
    context.interface = connectionStr;
    context.authenticated = FALSE;
    context.remote = TRUE;
    context.printf = RemoteInterfacePrintfImpl;
    context.gets = RemoteInterfaceGetsImpl;
    context.privateArg = connection;
    context.commands = ConnectionCommands;

    socketfd = connection->socketfd;
    fp = connection->fp;

    pfd[0].fd = socketfd;
    pfd[0].events = POLLIN;

    PrintResponse(fp,COMMAND_OK, "Ready");

    while (!remoteIntfExit && connection->connected)
    {
        int r;
        pfd[0].revents = 0;
        r = poll(pfd, 1, 30000);
        if (pfd[0].revents & POLLIN)
        {
            char *nl;
            // Read in the command
            if (fgets(line, MAX_LINE_LENGTH, fp))
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
                PrintResponse(fp,context.errorNumber, context.errorMessage);
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
    fclose(fp);
    connection->connected = FALSE;

    LogModule(LOG_DEBUG,REMOTEINTERFACE,"Connection thread exiting.\n");
    pthread_detach(connection->thread);
    RemoveConnection(connection);
}

static void PrintResponse(FILE *fp, uint16_t errno, char * msg)
{
    fprintf(fp, "%s%d %s\n", responselineStart, errno, msg);
    fflush(fp);
}

static int RemoteInterfacePrintfImpl(CommandContext_t *context, const char *format, va_list args)
{
    Connection_t *connection = (Connection_t*)context->privateArg;
    return vfprintf(connection->fp, format, args);
}

static char *RemoteInterfaceGetsImpl(CommandContext_t *context, char *buffer, int len)
{
    Connection_t *connection = (Connection_t*)context->privateArg;
    return fgets(buffer, len, connection->fp);
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
    char connectionStr[MAX_CONNECTION_STR_LENGTH];
    ListIterator_t iterator;

    for (ListIterator_Init(iterator, connectionsList);
         ListIterator_MoreEntries(iterator);
         ListIterator_Next(iterator))
    {
        Connection_t *connection = (Connection_t*)ListIterator_Current(iterator);
        if (connection->connected)
        {
            GetConnectionString(&connection->clientAddress, connectionStr);
            CommandPrintf("%s\n", connectionStr);
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

static void GetConnectionString(struct sockaddr_storage *connAddr, char *output)
{
    if (connAddr->ss_family == AF_INET)
    {
        inet_ntop(connAddr->ss_family, &((struct sockaddr_in*)connAddr)->sin_addr, output, INET_ADDRSTRLEN);

        sprintf(output + strlen(output), ":%d", ((struct sockaddr_in*)connAddr)->sin_port);
    }
    else if (connAddr->ss_family == AF_INET6)
    {
        *output = '[';
        inet_ntop(connAddr->ss_family, &((struct sockaddr_in*)connAddr)->sin_addr, output + 1, INET6_ADDRSTRLEN);

        sprintf(output + strlen(output), "]:%d", ((struct sockaddr_in*)connAddr)->sin_port);
    }
    else
    {
        strcpy(output, "<unknown>");
        LogModule(LOG_ERROR, REMOTEINTERFACE, "Unknown family %d\n", connAddr->ss_family);
    }
}
