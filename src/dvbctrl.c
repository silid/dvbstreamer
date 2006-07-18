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
#include <ctype.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/types.h>
#include <linux/dvb/frontend.h>

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

/* For use when only an RERR message is expected */
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
        printlog(LOG_ERROR, "Unexpected response message! (type 0x%02x)\n",\
                 MessageGetCode(&message) );\
        return;\
    }\
    }while(0)

/* For use when a message other than RERR is expected */
#define CHECK_EXPECTED(_expected) \
    do{\
    if (MessageGetCode(&message) == MSGCODE_RERR) \
    {\
        uint8_t code = 0;\
        char *text = NULL;\
        MessageReadUint8(&message, &code);\
        MessageReadString(&message, &text);\
        printf("ERROR (%d) %s\n", code, text);\
        free(text);\
        return;\
    }\
    else if (MessageGetCode(&message) != (_expected))\
    {\
        printlog(LOG_ERROR, "Unexpected response message! (type 0x%02x)",\
                 MessageGetCode(&message) );\
        return;\
    }\
    }while(0)

#define AUTHENTICATE() \
    do{\
        if (!Authenticate())\
        {\
            printf("Failed to Authenticate username/password!\n");\
        }\
    }while(0)



typedef struct ServiceOutput_t
{
    char *name;
    char *destination;
    char *service;
}ServiceOutput_t;

typedef struct ServiceOutputList_t
{
    int nrofOutputs;
    ServiceOutput_t *outputs;
}ServiceOutputList_t;

typedef struct ManualOutput_t
{
    char *name;
    char *destination;
}ManualOutput_t;

typedef struct ManualOutputList_t
{
    int nrofOutputs;
    ManualOutput_t *outputs;
}ManualOutputList_t;

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
static bool Authenticate();
static int ParsePID(char *argument);
static ServiceOutputList_t* GetServiceOutputs();
static void FreeServiceOutputs(ServiceOutputList_t *list);
static ManualOutputList_t* GetManualOutputs();
static void FreeManualOutputs(ManualOutputList_t *outputs);

static void CommandInfo(char *argv[]);
static void CommandServices(char *argv[]);
static void CommandSelect(char *argv[]);
static void CommandCurrent(char *argv[]);
static void CommandPids(char *argv[]);
static void CommandStats(char *argv[]);
static void CommandAddOutput(char *argv[]);
static void CommandRmOutput(char *argv[]);
static void CommandOutputs(char *argv[]);
static void CommandAddRmPID(char *argv[]);
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

static char pidsCmd[] = "pids";
static char lspidsCmd[] ="lspids";
static char servicesCmd[] = "services";
static char multiplexCmd[]= "multiplex";
static char addpidCmd[] = "addpid";
static char rmpidCmd[]  = "rmpid";
static char addoutputCmd[] = "addoutput";
static char addsfCmd[] = "addsf";
static char rmoutputCmd[] = "rmoutput";
static char rmsfCmd[] = "rmsf";

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
            servicesCmd, 0,
            "List all available services.",
            CommandServices
        },
        {
            multiplexCmd, 0,
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
            pidsCmd, 1,
            "List the PIDs for a specified service",
            CommandPids
        },
        {
            "stats", 0,
            "Display the stats for the PAT,PMT and service PID filters",
            CommandStats
        },
        {
            addoutputCmd, 2,
            "Takes <output name> <ipaddress>:<udp port>\n"
            "Adds a new destination for sending packets to. This is only used for "
            "manually filtered packets. "
            "To send packets to this destination you'll need to also call \'filterpid\' "
            "with this output as an argument.",
            CommandAddOutput
        },
        {
            rmoutputCmd, 1,
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
            addpidCmd, 2,
            "Takes <output name> <pid>\n"
            "Adds a PID to the filter to be sent to the specified output.",
            CommandAddRmPID
        },
        {
            rmpidCmd, 2,
            "Takes <output name> <pid>\n"
            "Removes the PID from the filter that is sending packets to the specified output.",
            CommandAddRmPID
        },
        {
            lspidsCmd, 1,
            "Takes <output name>\n"
            "List the PIDs being filtered for a specific output",
            CommandPids
        },
        {
            addsfCmd, 2,
            "Takes <output name> <ipaddress>:<udp port>\n"
            "Adds a new destination for sending a secondary service to.",
            CommandAddOutput
        },
        {
            rmsfCmd, 1,
            "Takes <output name>\n"
            "Remove a destination for sending secondary services to.",
            CommandRmOutput
        },
        {
            "lssfs", 0,
            "List all secondary service filters their names, destinations and currently selected service.",
            CommandListSFS
        },
        {
            "setsf", 2,
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

    MessageInit(&message, MSGCODE_INFO);

    MessageEncode(&message, "b", infoParams[found].value);

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
    uint16_t servicecount = 0;
    int i;

    MessageReset(&message);
    if (strcmp(servicesCmd, argv[0]) == 0)
    {
        MessageSetCode(&message, MSGCODE_SSLA);
    }
    if (strcmp(multiplexCmd, argv[0]) == 0)
    {
        MessageSetCode(&message, MSGCODE_SSLM);
    }

    MESSAGE_SENDRECV();

    CHECK_EXPECTED(MSGCODE_RLS);

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

static void CommandSelect(char *argv[])
{
    if (!Authenticate())
    {
        printf("Failed to authenticate username/password!\n");
        return;
    }

    MessageInit(&message, MSGCODE_CSPS);
    MessageEncode(&message, "s", argv[1]);

    MESSAGE_SENDRECV();

    CHECK_RERR_OK();
}

static void CommandCurrent(char *argv[])
{
    MessageInit(&message, MSGCODE_SSPS);

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
{
    uint16_t i;
    uint16_t pidCount = 0;
    uint16_t pid;

    if (strcmp(pidsCmd, argv[0]) == 0)
    {
        MessageInit(&message, MSGCODE_SSPL);
    }
    else
    {
        MessageInit(&message, MSGCODE_SOLP);
    }

    MessageEncode(&message, "s", argv[1]);

    MESSAGE_SENDRECV();

    CHECK_EXPECTED(MSGCODE_RLP);

    MessageReadUint16(&message, &pidCount);
    printlog(LOG_DEBUG, "pidCount=%u\n", (unsigned int)pidCount);
    for ( i = 0; i < pidCount; i ++)
    {
        MessageReadUint16(&message, &pid);
        printf("0x%04x\n", pid);
    }
}

static void CommandStats(char *argv[])
{
    uint32_t bitrate = 0;
    uint32_t totalPC = 0;
    uint32_t patPC = 0;
    uint32_t pmtPC = 0;
    uint32_t sdtPC = 0;
    int i;
    ServiceOutputList_t *serviceOutputs = NULL;
    ManualOutputList_t *manualOutputs = NULL;
    MessageInit(&message, MSGCODE_STSS);
    MESSAGE_SENDRECV();
    CHECK_EXPECTED(MSGCODE_RTSS);

    MessageDecode(&message, "lllll", &bitrate, &totalPC, &patPC, &pmtPC, &sdtPC);

    printf("PSI/SI Processor Statistics\n"
           "---------------------------\n");

    printf("\t%-15s : %u\n", "PAT", (unsigned int)patPC);
    printf("\t%-15s : %u\n", "PMT", (unsigned int)pmtPC);
    printf("\t%-15s : %u\n", "SDT", (unsigned int)sdtPC);

    printf("\n");

    printf("Service Filter Statistics\n"
           "-------------------------\n");

    serviceOutputs = GetServiceOutputs();
    if (serviceOutputs)
    {
        for (i = 0; i < serviceOutputs->nrofOutputs; i ++)
        {
            uint32_t pc = 0;
            MessageInit(&message, MSGCODE_SSPC);
            MessageEncode(&message, "s", serviceOutputs->outputs[i].name);

            MESSAGE_SENDRECV();

            if (MessageGetCode(&message) == MSGCODE_ROPC)
            {
                MessageDecode( &message, "l", &pc);
                printf("\t%-15s : %u\n", serviceOutputs->outputs[i].name, (unsigned int)pc);
            }
        }
        FreeServiceOutputs(serviceOutputs);
    }
    printf("\n");

    printf("Manual Output Statistics\n"
           "------------------------\n");
    manualOutputs = GetManualOutputs();
    if (manualOutputs)
    {
        for (i = 0; i < manualOutputs->nrofOutputs; i ++)
        {
            uint32_t pc = 0;
            MessageInit(&message, MSGCODE_SOPC);
            MessageEncode(&message, "s", manualOutputs->outputs[i].name);

            MESSAGE_SENDRECV();

            if (MessageGetCode(&message) == MSGCODE_ROPC)
            {
                MessageDecode( &message, "l", &pc);
                printf("\t%-15s : %u\n", manualOutputs->outputs[i].name, (unsigned int)pc);
            }
        }
        FreeManualOutputs(manualOutputs);
    }
    printf("\n");

    printf("Total packets processed: %u\n", (unsigned int)totalPC);
    printf("Approximate TS bitrate : %gMbs\n", ((double)bitrate / (1024.0 * 1024.0)));
}

static void CommandAddOutput(char *argv[])
{
    AUTHENTICATE();

    if (strcmp(addoutputCmd, argv[0]) == 0)
    {
        MessageInit(&message, MSGCODE_COAO);
    }
    else
    {
        MessageInit(&message, MSGCODE_CSSA);
    }
    MessageEncode(&message, "ss", argv[1], argv[2]);

    MESSAGE_SENDRECV();

    CHECK_RERR_OK();
}

static void CommandRmOutput(char *argv[])
{
    AUTHENTICATE();

    if (strcmp(rmoutputCmd, argv[0]) == 0)
    {
        MessageInit(&message, MSGCODE_CORO);
    }
    else
    {
        MessageInit(&message, MSGCODE_CSSR);
    }
    MessageEncode(&message, "s", argv[1]);

    MESSAGE_SENDRECV();

    CHECK_RERR_OK();
}

static void CommandOutputs(char *argv[])
{
    int i;
    ManualOutputList_t *outputs = NULL;

    outputs = GetManualOutputs();
    if (outputs)
    {
        for (i = 0 ; i < outputs->nrofOutputs; i ++)
        {
            printf("%-15s : %s\n", outputs->outputs[i].name, outputs->outputs[i].destination);
        }
        FreeManualOutputs(outputs);
    }
}

static void CommandAddRmPID(char *argv[])
{
    int pid = ParsePID(argv[2]);
    if (pid == -1)
    {
        printf("Failed to parse \"%s\" as a PID!\n", argv[2]);
        return;
    }
    AUTHENTICATE();
    if (strcmp(addpidCmd, argv[0]) == 0)
    {
        MessageInit(&message, MSGCODE_COAP);
    }
    else
    {
        MessageInit(&message, MSGCODE_CORP);
    }
    MessageEncode(&message, "sdd", argv[1], 1, pid);

    MESSAGE_SENDRECV();

    CHECK_RERR_OK();
}

static void CommandListSFS(char *argv[])
{
    int i;
    ServiceOutputList_t *outputs = NULL;

    outputs = GetServiceOutputs();
    if (outputs)
    {
        for (i = 0; i < outputs->nrofOutputs; i ++)
        {
            printf("%-15s : %s (%s)\n",
                outputs->outputs[i].name,
                outputs->outputs[i].destination,
                outputs->outputs[i].service[0] ? outputs->outputs[i].service:"<None>");
        }
        FreeServiceOutputs(outputs);
    }
}

static void CommandSetSF(char *argv[])
{
    AUTHENTICATE();

    MessageInit(&message, MSGCODE_CSSS);
    MessageEncode(&message, "ss", argv[1], argv[2]);

    MESSAGE_SENDRECV();

    CHECK_RERR_OK();
}

static void CommandFEStatus(char *argv[])
{
    uint8_t status = 0;
    uint32_t ber = 0;
    uint16_t snr = 0;
    uint16_t strength = 0;
    MessageInit(&message, MSGCODE_SFES);

    MESSAGE_SENDRECV();

    CHECK_EXPECTED(MSGCODE_RFES);

    MessageDecode(&message, "bldd", &status,&ber,&snr,&strength);

    printf("Tuner status:  %s%s%s%s%s%s\n",
           (status & FE_HAS_SIGNAL)?"Signal, ":"",
           (status & FE_TIMEDOUT)?"Timed out, ":"",
           (status & FE_HAS_LOCK)?"Lock, ":"",
           (status & FE_HAS_CARRIER)?"Carrier, ":"",
           (status & FE_HAS_VITERBI)?"VITERBI, ":"",
           (status & FE_HAS_SYNC)?"Sync, ":"");
    printf("BER = %u Signal Strength = %u SNR = %u\n", (unsigned int)ber, (unsigned int)strength, (unsigned int)snr);
}

static bool Authenticate()
{
    bool authenticated = FALSE;
    if (!username)
    {
        printlog(LOG_ERROR, "No username supplied!\n");
        return authenticated;
    }
    if (!password)
    {
        printlog(LOG_ERROR, "No password supplied!\n");
        return authenticated;
    }
    MessageInit(&message, MSGCODE_AUTH);
    MessageEncode(&message, "ss", username, password );

    MESSAGE_SENDRECV();

    if (MessageGetCode(&message) == MSGCODE_RERR)
    {
        uint8_t code = 0;
        MessageReadUint8(&message, &code);
        authenticated = (code == 0);
    }
    else
    {
        printlog(LOG_ERROR, "Unexpected response message! (type 0x%02x)\n",
                 MessageGetCode(&message) );
    }

    return authenticated;
}

static int ParsePID(char *argument)
{
    char *formatstr;
    int pid;

    if ((argument[0] == '0') && (tolower(argument[1])=='x'))
    {
        argument[1] = 'x';
        formatstr = "0x%hx";
    }
    else
    {
        formatstr = "%hd";
    }

    if (sscanf(argument, formatstr, &pid) == 0)
    {
        return -1;
    }

    return pid;
}

static ServiceOutputList_t* GetServiceOutputs()
{
    ServiceOutputList_t *result = NULL;
    uint8_t nrofOutputs = 0;
    int i;
    MessageInit(&message, MSGCODE_SSFL);

    MESSAGE_SENDRECV();

    if (MessageGetCode(&message) == MSGCODE_RERR)
    {
        uint8_t code = 0;
        char *text = NULL;
        MessageReadUint8(&message, &code);
        MessageReadString(&message, &text);
        printf("ERROR (%d) %s\n", code, text);
        free(text);
        return NULL;
    }
    else if (MessageGetCode(&message) != MSGCODE_RSSL)
    {
        printlog(LOG_ERROR, "Unexpected response message! (type 0x%02x)",
                 MessageGetCode(&message) );
        return NULL;
    }

    MessageDecode(&message, "b", &nrofOutputs);
    result = calloc(1, sizeof(ServiceOutputList_t));
    if (!result)
    {
        return result;
    }
    result->nrofOutputs = nrofOutputs & 0xff;
    result->outputs = calloc(result->nrofOutputs, sizeof(ServiceOutput_t));
    if (!result->outputs)
    {
        free(result);
        return NULL;
    }
    for (i = 0; i < result->nrofOutputs; i ++)
    {
        MessageDecode(&message, "sss",
            &result->outputs[i].name,
            &result->outputs[i].destination,
            &result->outputs[i].service);
    }
    return result;
}

static void FreeServiceOutputs(ServiceOutputList_t *list)
{
    int i;
    for (i = 0 ; i < list->nrofOutputs; i ++)
    {
        if (list->outputs[i].name)
        {
            free(list->outputs[i].name);
        }
        if (list->outputs[i].destination)
        {
            free(list->outputs[i].destination);
        }
        if (list->outputs[i].service)
        {
            free(list->outputs[i].service);
        }
    }
    free(list->outputs);
    free(list);
}

static ManualOutputList_t* GetManualOutputs()
{
    ManualOutputList_t *result = NULL;
    uint8_t nrofOutputs = 0;
    int i;
    MessageInit(&message, MSGCODE_SOLO);

    MESSAGE_SENDRECV();

    if (MessageGetCode(&message) == MSGCODE_RERR)
    {
        uint8_t code = 0;
        char *text = NULL;
        MessageReadUint8(&message, &code);
        MessageReadString(&message, &text);
        printf("ERROR (%d) %s\n", code, text);
        free(text);
        return NULL;
    }
    else if (MessageGetCode(&message) != MSGCODE_ROLO)
    {
        printlog(LOG_ERROR, "Unexpected response message! (type 0x%02x)",
                 MessageGetCode(&message) );
        return NULL;
    }

    MessageDecode(&message, "b", &nrofOutputs);

    result = calloc(1, sizeof(ManualOutputList_t));
    if (!result)
    {
        return result;
    }
    result->nrofOutputs = nrofOutputs & 0xff;
    result->outputs = calloc(result->nrofOutputs, sizeof(ManualOutput_t));
    if (!result->outputs)
    {
        free(result);
        return NULL;
    }
    for (i = 0; i < result->nrofOutputs; i ++)
    {
        MessageDecode(&message, "ss",
            &result->outputs[i].name,
            &result->outputs[i].destination);
    }
    return result;
}

static void FreeManualOutputs(ManualOutputList_t *list)
{
    int i;
    for (i = 0 ; i < list->nrofOutputs; i ++)
    {
        if (list->outputs[i].name)
        {
            free(list->outputs[i].name);
        }
        if (list->outputs[i].destination)
        {
            free(list->outputs[i].destination);
        }
    }
    free(list->outputs);
    free(list);
}

