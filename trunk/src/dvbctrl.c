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
#include "remoteintf.h"

#define MAX_LINE_LENGTH 256

static void usage(char *appname);
static void version(void);
static bool Authenticate();
static void StripNewLineFromEnd(char *str);
static void ProcessResponseLine(char *line, char **ver, int *errno, char **errmsg);

static bool SendCommand(FILE *socketfp, char *line, char **version, int *errno, char **errmsg);

static char responselineStart[] = "DVBStreamer/";
static char *host = "localhost";
static int adapterNumber = 0;
static char *username = NULL;
static char *password = NULL;
static char line[MAX_LINE_LENGTH];

int main(int argc, char *argv[])
{
    int i;
    socklen_t address_len;
#ifdef __CYGWIN__
    struct hostent *hostinfo;
    struct sockaddr_in address;
#else
    struct sockaddr_storage address;
    struct addrinfo *addrinfo, hints;
#endif    
    char portnumber[10];    
    char *filename = NULL;
    int socketfd = -1;
    FILE *socketfp;
    char *ver;
    int errno;
    char *errmsg;

    while (TRUE)
    {
        int c;
        c = getopt(argc, argv, "vVh:a:u:p:f:");
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
            case 'f':
                filename = optarg;
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
#ifdef __CYGWIN__
    address.sin_port = htons(REMOTEINTERFACE_PORT + adapterNumber);
    hostinfo = gethostbyname(host);
    if (hostinfo == NULL)
    {
        printlog(LOG_ERROR, "Failed to find address for \"%s\"\n", host);
    }
    address.sin_family = hostinfo->h_addrtype;
    memcpy((char *)&(address.sin_addr), hostinfo->h_addr, hostinfo->h_length);
    address_len = sizeof(address);
    socketfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
    sprintf(portnumber, "%d", REMOTEINTERFACE_PORT + adapterNumber);
    
    memset((void *)&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_ADDRCONFIG;
    if ((getaddrinfo(host, portnumber, &hints, &addrinfo) != 0) || (addrinfo == NULL))
    {
        printlog(LOG_ERROR, "Failed to get address\n");
        exit(1);
    }

    if (addrinfo->ai_addrlen > sizeof(struct sockaddr_storage))
    {
        printlog(LOG_ERROR, "Failed to parse address\n");
        freeaddrinfo(addrinfo);
        exit(1);
    }
    address_len = addrinfo->ai_addrlen;
    memcpy(&address, addrinfo->ai_addr, addrinfo->ai_addrlen);
    freeaddrinfo(addrinfo);
    socketfd = socket(address.ss_family, SOCK_STREAM, IPPROTO_TCP);
#endif
    if (socketfd < 0)
    {
        printlog(LOG_ERROR, "Failed to create socket!\n");
        exit(1);
    }

    if (connect(socketfd, (const struct sockaddr *) &address, address_len))
    {
        printlog(LOG_ERROR, "Failed to connect to host %s port %d\n",
                 host, REMOTEINTERFACE_PORT + adapterNumber);
        exit(1);
    }
    printlog(LOG_DEBUG, "Socket connected to host %s port %d\n",
             host, REMOTEINTERFACE_PORT + adapterNumber);

    socketfp = fdopen(socketfd, "r+");
    if (!fgets(line , MAX_LINE_LENGTH, socketfp))
    {
        printlog(LOG_ERROR, "No ready line received from server!\n");
        fclose(socketfp);
        return 1;
    }

    ProcessResponseLine(line, &ver, &errno, &errmsg);
    if (errno != 0)
    {
        printlog(LOG_ERROR, "%s\n", errmsg);
        fclose(socketfp);
        return errno;
    }

    if (username && password)
    {
        if (!Authenticate(socketfp))
        {
            printlog(LOG_ERROR, "Failed to authenticate!");
            fclose(socketfp);
            return 1;
        }
    }

    /* Process commands */

    if (filename)
    {
        FILE *fp = fopen(filename, "r");
        if (!fp)
        {
            printlog(LOG_ERROR,"Failed to open %s\n", filename);
            fclose(socketfp);
            return 1;
        }
        while(fgets(line, MAX_LINE_LENGTH, fp))
        {
            StripNewLineFromEnd(line);
            SendCommand(socketfp, line, &ver, &errno, &errmsg);
            if (errno != 0)
            {
                printlog(LOG_ERROR, "%s\n", errmsg);
                fclose(socketfp);
                return errno;
            }
        }
    }
    else
    {
        line[0] = 0;
        for (i = optind; i < argc; i ++)
        {
            if (i - optind)
            {
                strcat(line, " ");
            }
            strcat(line, argv[i]);
        }

        SendCommand(socketfp, line, &ver, &errno, &errmsg);
        if (errno != 0)
        {
            printlog(LOG_ERROR, "%s\n", errmsg);
            fclose(socketfp);
            return errno;
        }
    }
    /* Disconnect from host */
    fclose(socketfp);
    printlog(LOG_DEBUG, "Socket closed\n");

    return 0;
}


/*
 * Output command line usage and help.
 */
static void usage(char *appname)
{
    fprintf(stderr, "Usage:%s [<options>] <commands>\n"
            "      Options:\n"
            "      -v            : Increase the amount of debug output, can be used multiple\n"
            "                      times for more output.\n"
            "      -V            : Print version information then exit.\n"
            "      -h host       : Host to control.\n"
            "      -a <adapter>  : DVB Adapter number to control on the host.\n"
            "      -f <file>     : Read commands from <file>.\n",
            appname
           );
}

/*
 * Output version and license conditions
 */
static void version(void)
{
    printf("%s - %s (Compiled %s %s)\n"
           "Written by Adam Charrett (charrea6@users.sourceforge.net).\n"
           "\n"
           "Copyright 2006 Adam Charrett\n"
           "This is free software; see the source for copying conditions.  There is NO\n"
           "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n",
           PACKAGE, VERSION, __DATE__, __TIME__);
}
/******************************************************************************/
/* Command functions                                                          */
/******************************************************************************/
static bool Authenticate(FILE *socketfp)
{
    char * ver;
    int errno;
    char *errmsg;
    sprintf(line, "auth %s %s", username, password);
    SendCommand(socketfp, line, &ver, &errno, &errmsg);
    return errno == 0;
}

static bool SendCommand(FILE *socketfp, char *cmd, char **ver, int *errno, char **errmsg)
{
    bool foundResponse = FALSE;
    char *separator;

    printlog(LOG_DEBUG, "Sending command \"%s\"\n", cmd);
    fprintf(socketfp, "%s\n", cmd);

    *ver = NULL;
    *errno = 0;
    *errmsg = NULL;
    do
    {
        if (fgets(line, MAX_LINE_LENGTH, socketfp))
        {
            StripNewLineFromEnd(line);
            printlog(LOG_DEBUG, "Got line \"%s\"\n", line);
            if (strncmp(line, responselineStart, sizeof(responselineStart) - 1) == 0)
            {
                separator = strchr(line + sizeof(responselineStart), '/');
                if (separator)
                {
                    ProcessResponseLine(line, ver, errno, errmsg);
                    foundResponse = TRUE;
                    break;
                }
            }
            puts(line);
        }
        else
        {
            break;
        }
    }while(!foundResponse);

    return foundResponse;
}


static void StripNewLineFromEnd(char *str)
{
    int len = strlen(str);

    if ((str[len - 1] == '\n') || (str[len - 1] == '\r'))
    {
        if ((str[len - 2] == '\n') || (str[len - 2] == '\r'))
        {
            str[len - 2] = 0;
        }
        else
        {
            str[len - 1] = 0;
        }
    }
}

static void ProcessResponseLine(char *line, char **ver, int *errno, char **errmsg)
{
    char *separator;

    *ver = NULL;
    *errno = 0;
    *errmsg = NULL;

    separator = strchr(line + sizeof(responselineStart), '/');
    if (separator)
    {
        char *start;
        *separator = 0;
        start = line + sizeof(responselineStart);
        *ver  = strdup(start);
        start = separator + 1;
        separator = strchr(separator + 1, ' ');
        if (separator)
        {
            *separator = 0;
            *errmsg = strdup(separator + 1);
        }
        *errno = atoi(start);
    }
}
