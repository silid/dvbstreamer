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

dvbctrl.c

Application to control dvbstreamer in daemon mode.

*/
#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "types.h"
#include "logging.h"
#include "binarycomms.h"
#include "messages.h"

#define MESSAGE_SENDRECV() \
    do{\
        if (MessageSend(&message, socketfd))\
        {\
            printlog(LOG_ERROR, "Sending message failed!\n");\
            exit(1);\
        }\
        if (MessageRecv(&message, socketfd))\
        {\
            printlog(LOG_ERROR, "Failed to receive message!\n");\
            exit(1);\
        }\
    }while(0)

#define CHECK_RERR_OK() \
    do{\
    if (MessageGetCode(&message) == MSGCODE_RERR) \
    {\
        uint8_t code = 0;\
        char *text = NULL;\
        MessageReadUint8(&message, &code);\
        if (code != 0)\
        {\
            MessageReadString(&message, &text);\
            printf("ERROR (%d) %s\n", code, text);\
            free(text);\
            return;\
        }\
    }\
    else\
    {\
        printlog(LOG_ERROR, "Unexpected response message! (type 0x%02x)",\
                 MessageGetCode(&message) );\
        return;\
    }\
    }while(0)

typedef struct InfoParam_t
{
    char *name;
    uint8_t value;
}
InfoParam_t;

typedef void (*CommandFunc_t)(char *argv[]);

typedef struct Command_t
{
    char *name;
    int nrofArgs;
    char *help;
    CommandFunc_t func;
}
Command_t;

static void usage(char *appname);
static void version(void);
static void CommandInfo(char *argv[]);
static void CommandServices(char *argv[]);
static void CommandSelect(char *argv[]);
static void CommandCurrent(char *argv[]);
static void CommandPids(char *argv[]);
static void CommandStats(char *argv[]);
static void CommandAddOutput(char *argv[]);
static void CommandRmOutput(char *argv[]);
static void CommandOutputs(char *argv[]);
static void CommandAddPID(char *argv[]);
static void CommandRmPID(char *argv[]);
static void CommandOutputPIDs(char *argv[]);
static void CommandAddSF(char *argv[]);
static void CommandRemoveSF(char *argv[]);
static void CommandListSFS(char *argv[]);
static void CommandSetSF(char *argv[]);
static void CommandFEStatus(char *argv[]);

/* Used by logging to determine whether to include date/time info */
int DaemonMode = FALSE;

static Message_t message;
static int socketfd = -1;
static char *host = "localhost";
static int adapterNumber = 0;
static char *username = NULL;
static char *password = NULL;

static Command_t commands[] =
    {
        {
            "info", 1,
            "Retrieves information about the host, use info <param> where param "
            "is name for the name of the host, fetype for the front end type, "
            "upsecs for the number of seconds the server has been running, uptime "
            "for a nice time string on how long the host has been running.",
            CommandInfo
        },
        {
            "services", 0,
            "List all available services.",
            CommandServices
        },
        {
            "multiplex", 0,
            "List all the services on the current multiplex.",
            CommandServices
        },
        {
            "select", 1,
            "Select the service to stream to the primary output.",
            CommandSelect
        },
        {
            "current", 0,
            "Print out the service currently being streamed.",
            CommandCurrent
        },
        {
            "pids", 1,
            "List the PIDs for a specified service",
            CommandPids
        },
        {
            "stats", 0,
            "Display the stats for the PAT,PMT and service PID filters",
            CommandStats
        },
        {
            "addoutput", 2,
            "Takes <output name> <ipaddress>:<udp port>\n"
            "Adds a new destination for sending packets to. This is only used for "
            "manually filtered packets. "
            "To send packets to this destination you'll need to also call \'filterpid\' "
            "with this output as an argument.",
            CommandAddOutput
        },
        {
            "rmoutput", 1,
            "Takes <output name>\n"
            "Removes the destination and stops all filters associated with this output.",
            CommandRmOutput
        },
        {
            "lsoutputs", 0,
            "List all active additonal output names and destinations.",
            CommandOutputs
        },
        {
            "addpid", 2,
            "Takes <output name> <pid>\n"
            "Adds a PID to the filter to be sent to the specified output.",
            CommandAddPID
        },
        {
            "rmpid", 2,
            "Takes <output name> <pid>\n"
            "Removes the PID from the filter that is sending packets to the specified output.",
            CommandRmPID
        },
        {
            "lspids", 1,
            "Takes <output name>\n"
            "List the PIDs being filtered for a specific output",
            CommandOutputPIDs
        },
        {
            "addsf", 2,
            "Takes <output name> <ipaddress>:<udp port>\n"
            "Adds a new destination for sending a secondary service to.",
            CommandAddSF
        },
        {
            "rmsf", 1,
            "Takes <output name>\n"
            "Remove a destination for sending secondary services to.",
            CommandRemoveSF
        },
        {
            "lssfs", 0,
            "List all secondary service filters their names, destinations and currently selected service.",
            CommandListSFS
        },
        {
            "setsf", 1,
            "Takes <output name> <service name>\n"
            "Stream the specified service to the secondary service output.",
            CommandSetSF
        },
        {
            "festatus", 0,
            "Displays whether the front end is locked, the bit error rate and signal to noise"
            "ratio and the signal strength",
            CommandFEStatus
        },
        {
            NULL, 0, NULL, NULL
        }
    };

static InfoParam_t infoParams[] =
    {
        { "name",   0x00 },
        { "fetype", 0x01 },
        { "upsecs", 0xfe },
        { "uptime", 0xff },
        { NULL,     0x00 },
    };

int main(int argc, char *argv[])
{
    int i, consumed;
    struct hostent *hostinfo;
    struct sockaddr_in sockaddr;

    while (TRUE)
    {
        char c;
        c = getopt(argc, argv, "vVh:a:u:p:");
        if (c == -1)
        {
            break;
        }
        switch (c)
        {
            case 'v':
                verbosity ++;
                break;
            case 'V':
                version();
                exit(0);
                break;
            case 'h':
                host = optarg;
                printlog(LOG_INFOV, "Will connect to host %s\n", host);
                break;
            case 'a':
                adapterNumber = atoi(optarg);
                printlog(LOG_INFOV, "Using adapter %d\n", adapterNumber);
                break;
            case 'u':
                username = optarg;
                break;
            case 'p':
                password = optarg;
                break;
            default:
                usage(argv[0]);
                exit(1);
        }
    }
    /* Commands follow options */
    if (optind >= argc)
    {
        printlog(LOG_ERROR, "No commands specified!\n");
        exit(1);
    }
    /* Connect to host */
    socketfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socketfd == -1)
    {
        printlog(LOG_ERROR, "Failed to create socket!\n");
        exit(1);
    }

    sockaddr.sin_port = htons(BINARYCOMMS_PORT + adapterNumber);
    hostinfo = gethostbyname(host);
    if (hostinfo == NULL)
    {
        printlog(LOG_ERROR, "Failed to find address for \"%s\"\n", host);
    }
    sockaddr.sin_family = hostinfo->h_addrtype;
    memcpy((char *)&(sockaddr.sin_addr), hostinfo->h_addr, hostinfo->h_length);

    if (connect(socketfd, (const struct sockaddr *) &sockaddr, sizeof(sockaddr)))
    {
        printlog(LOG_ERROR, "Failed to connect to host %s:%d\n",
                 inet_ntoa(sockaddr.sin_addr), BINARYCOMMS_PORT + adapterNumber);
        exit(1);
    }
    printlog(LOG_DEBUG, "Socket connected to %s:%d\n",
             inet_ntoa(sockaddr.sin_addr), BINARYCOMMS_PORT + adapterNumber);

    /* Process commands */
    consumed = 0;
    for (i = optind; i < argc; i += consumed)
    {
        int c;
        bool found = FALSE;
        for (c = 0; commands[c].name; c ++)
        {
            if (strcasecmp(argv[i], commands[c].name) == 0)
            {
                consumed = 1 + commands[c].nrofArgs;
                commands[c].func(&argv[i]);
                found = TRUE;
            }
        }
        if (!found)
        {
            printlog(LOG_ERROR, "Unknown command \"%s\"\n", argv[i]);
            break;
        }
    }

    /* Disconnect from host */
    close(socketfd);
    printlog(LOG_DEBUG, "Socket closed\n");

    return 0;
}


/*
 * Output command line usage and help.
 */
static void usage(char *appname)
{
    int c;
    fprintf(stderr, "Usage:%s [<options>] <commands>\n"
            "      Options:\n"
            "      -v            : Increase the amount of debug output, can be used multiple\n"
            "                      times for more output\n"
            "      -V            : Print version information then exit\n"
            "      -h host       : Host to control\n"
            "      -a <adapter>  : DVB Adapter number to control on the host\n",
            appname
           );
    fprintf(stderr, "\nCommands include:\n");
    for (c = 0; commands[c].name; c ++)
    {
        fprintf(stderr, "%10s:\n%s\n\n", commands[c].name, commands[c].help);
    }
}

/*
 * Output version and license conditions
 */
static void version(void)
{
    printf("%s - %s\n"
           "Written by Adam Charrett (charrea6@users.sourceforge.net).\n"
           "\n"
           "Copyright 2006 Adam Charrett\n"
           "This is free software; see the source for copying conditions.  There is NO\n"
           "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n",
           PACKAGE, VERSION);
}
/******************************************************************************/
/* Command functions                                                          */
/******************************************************************************/
static void CommandInfo(char *argv[])
{
    int i;
    int found = -1;

    for (i = 0; infoParams[i].name; i ++)
    {
        if (strcasecmp(infoParams[i].name, argv[1]) == 0)
        {
            found = i;
            break;
        }
    }
    if (found < 0)
    {
        printlog(LOG_ERROR, "Unknown info \"%s\"\n", argv[1]);
        return ;
    }
    printlog(LOG_DEBUG, "Querying host for \"%s\"\n", infoParams[found].name);

    MessageEncode(&message, MSGCODE_INFO, "b", infoParams[found].value);

    MESSAGE_SENDRECV();

    if (MessageGetCode(&message) == MSGCODE_RERR)
    {
        uint8_t code;
        char *text = NULL;
        MessageDecode(&message, "bs", &code, &text);
        if (code == 0)
        {
            printf("%s\n", text);
        }
        else
        {
            printf("ERROR (%d) %s\n", code, text);
        }
    }
    else
    {
        printlog(LOG_ERROR, "Unexpected response message! (type 0x%02x)",
                 MessageGetCode(&message) );
    }
}

static void CommandServices(char *argv[])
{
    MessageReset(&message);
    if (strcmp("services", argv[0]) == 0)
    {
        MessageSetCode(&message, MSGCODE_SSLA);
    }
    if (strcmp("multiplex", argv[0]) == 0)
    {
        MessageSetCode(&message, MSGCODE_SSLM);
    }

    MESSAGE_SENDRECV();

    if (MessageGetCode(&message) == MSGCODE_RSL)
    {
        uint16_t servicecount = 0;
        int i;
        MessageReadUint16( &message, &servicecount);

        for (i = 0; i < servicecount; i ++)
        {
            char *name = NULL;
            MessageReadString( &message, &name);
            if (name)
            {
                printf("%s\n", name);
                free(name);
            }
        }
    }
    else if (MessageGetCode(&message) == MSGCODE_RERR)
    {
        uint8_t code;
        char *reason = NULL;
        MessageDecode(&message, "bs", &code, &reason);
        printlog(LOG_ERROR, "Failed to retrieve service list, code 0x%02x reason \"%s\"\n",
                 code, reason);
        if (reason)
        {
            free(reason);
        }
    }
    else
    {
        printlog(LOG_ERROR, "Unexpected response message! (type 0x%02x)",
                 MessageGetCode(&message) );
    }
}

static void CommandSelect(char *argv[])
{
    if (!username)
    {
        printlog(LOG_ERROR, "No username supplied!\n");
        return ;
    }
    if (!password)
    {
        printlog(LOG_ERROR, "No password supplied!\n");
        return ;
    }
    MessageEncode(&message, MSGCODE_AUTH, "ss", username, password );

    MESSAGE_SENDRECV();

    CHECK_RERR_OK();

    MessageEncode(&message, MSGCODE_CSPS, "s", argv[1]);

    MESSAGE_SENDRECV();

    CHECK_RERR_OK();
}

static void CommandCurrent(char *argv[])
{
    MessageReset(&message);
    MessageSetCode(&message, MSGCODE_SSPC);

    MESSAGE_SENDRECV();

    if (MessageGetCode(&message) == MSGCODE_RERR)
    {
        uint8_t code;
        char *text = NULL;
        MessageDecode(&message, "bs", &code, &text);
        if (code == 0)
        {
            printf("%s\n", text);
        }
        else
        {
            printf("ERROR (%d) %s\n", code, text);
        }
    }
    else
    {
        printlog(LOG_ERROR, "Unexpected response message! (type 0x%02x)",
                 MessageGetCode(&message) );
    }
}

static void CommandPids(char *argv[])
{}

static void CommandStats(char *argv[])
{}

static void CommandAddOutput(char *argv[])
{}

static void CommandRmOutput(char *argv[])
{}

static void CommandOutputs(char *argv[])
{}

static void CommandAddPID(char *argv[])
{}

static void CommandRmPID(char *argv[])
{}

static void CommandOutputPIDs(char *argv[])
{}

static void CommandAddSF(char *argv[])
{}

static void CommandRemoveSF(char *argv[])
{}

static void CommandListSFS(char *argv[])
{}

static void CommandSetSF(char *argv[])
{}

static void CommandFEStatus(char *argv[])
{}
