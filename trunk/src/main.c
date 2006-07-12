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

main.c

Entry point to the application.

*/
#include "config.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <getopt.h>
#include <ctype.h>
#include <signal.h>
#include <sys/unistd.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "parsezap.h"
#include "dbase.h"
#include "multiplexes.h"
#include "services.h"
#include "dvb.h"
#include "ts.h"
#include "udpoutput.h"
#include "main.h"
#include "patprocessor.h"
#include "pmtprocessor.h"
#include "sdtprocessor.h"
#include "servicefilter.h"
#include "cache.h"
#include "logging.h"
#include "commands.h"
#include "outputs.h"
#include "binarycomms.h"


static void usage(char *appname);
static void version(void);
static void sighandler(int signum);
static void installsighandler(void);
static void InitDaemon(int adapter);
static void DeinitDaemon(void);

volatile Multiplex_t *CurrentMultiplex = NULL;
volatile Service_t *CurrentService = NULL;
PIDFilter_t *PIDFilters[PIDFilterIndex_Count];
TSFilter_t *TSFilter;
DVBAdapter_t *DVBAdapter;

bool ExitProgram = FALSE;
bool DaemonMode = FALSE;
char PrimaryService[] = "<Primary>";
Output_t *PrimaryServiceOutput = NULL;

static char PidFile[PATH_MAX];

int main(int argc, char *argv[])
{
    char *startupFile = NULL;
    fe_type_t channelsFileType = FE_OFDM;
    char channelsFile[PATH_MAX];
    void *outputArg = NULL;
    int i;
    int adapterNumber = 0;

    char *username = "dvbstreamer";
    char *password = "control";
    char *serverName = NULL;
    char *primaryOutput = NULL;

    channelsFile[0] = 0;
    installsighandler();

    while (!ExitProgram)
    {
        char c;
        c = getopt(argc, argv, "vVdo:a:t:s:c:f:u:p:n:");
        if (c == -1)
        {
            break;
        }
        switch (c)
        {
                case 'v': verbosity ++;
                break;
                case 'V':
                version();
                exit(0);
                break;
                case 'o':
                primaryOutput = optarg;
                break;
                case 'a': adapterNumber = atoi(optarg);
                printlog(LOG_INFOV, "Using adapter %d\n", adapterNumber);
                break;
                case 'f': startupFile = strdup(optarg);
                printlog(LOG_INFOV, "Using startup script %s\n", startupFile);
                break;
                /* Database initialisation options*/
                case 't':
                strcpy(channelsFile, optarg);
                channelsFileType = FE_OFDM;
                printlog(LOG_INFOV, "Using DVB-T channels file %s\n", channelsFile);
                break;
                case 's':
                strcpy(channelsFile, optarg);
                channelsFileType = FE_QPSK;
                printlog(LOG_INFOV, "Using DVB-S channels file %s\n", channelsFile);
                break;
                case 'c':
                strcpy(channelsFile, optarg);
                channelsFileType = FE_QAM;
                printlog(LOG_INFOV, "Using DVB-C channels file %s\n", channelsFile);
                break;
                /* Daemon options */
                case 'd': DaemonMode = TRUE;
                break;
                case 'u': username = optarg;
                break;
                case 'p': password = optarg;
                break;
                case 'n': serverName = optarg;
                break;
                default:
                usage(argv[0]);
                exit(1);
        }
    }

    if (ExitProgram)
    {
        exit(1);
    }

    if (DaemonMode)
    {
        InitDaemon( adapterNumber);
    }

    if (primaryOutput == NULL)
    {
        printlog(LOG_ERROR, "No output set!\n");
        usage(argv[0]);
        exit(1);
    }

    if (DBaseInit(adapterNumber))
    {
        printlog(LOG_ERROR, "Failed to initialise database\n");
        exit(1);
    }
    printlog(LOG_DEBUGV, "Database initalised\n");

    if (CacheInit())
    {
        printlog(LOG_ERROR,"Failed to initialise cache\n");
        exit(1);
    }
    printlog(LOG_DEBUGV, "Cache initalised\n");

    if (strlen(channelsFile))
    {
        printlog(LOG_INFO,"Importing services from %s\n", channelsFile);
        if (!parsezapfile(channelsFile, channelsFileType))
        {
            exit(1);
        }
        printlog(LOG_DEBUGV, "Channels file imported\n");
    }

    printlog(LOG_INFO, "%d Services available on %d Multiplexes\n", ServiceCount(), MultiplexCount());

    /* Initialise the DVB adapter */
    DVBAdapter = DVBInit(adapterNumber);
    if (!DVBAdapter)
    {
        printlog(LOG_ERROR, "Failed to open DVB adapter!\n");
        exit(1);
    }
    printlog(LOG_DEBUGV, "DVB adapter initalised\n");

    DVBDemuxStreamEntireTSToDVR(DVBAdapter);
    printlog(LOG_DEBUGV, "Streaming complete TS to DVR done\n");

    /* Create Transport stream filter thread */
    TSFilter = TSFilterCreate(DVBAdapter);
    if (!TSFilter)
    {
        printlog(LOG_ERROR, "Failed to create filter!\n");
        exit(1);
    }
    printlog(LOG_DEBUGV, "TSFilter created\n");

    /* Create PAT/PMT/SDT filters */
    PIDFilters[PIDFilterIndex_PAT] = PATProcessorCreate(TSFilter);
    PIDFilters[PIDFilterIndex_PMT] = PMTProcessorCreate(TSFilter);
    PIDFilters[PIDFilterIndex_SDT] = SDTProcessorCreate(TSFilter);

    /* Enable all the filters */
    for (i = 0; i < PIDFilterIndex_Count; i ++)
    {
        PIDFilters[i]->enabled = TRUE;
    }
    printlog(LOG_DEBUGV, "PID filters started\n");

    if (OutputsInit())
    {
        printlog(LOG_ERROR, "OutputsInit failed!\n");
    }

    /* Create Service filter */
    PrimaryServiceOutput = OutputAllocate(PrimaryService, OutputType_Service, primaryOutput);
    if (!PrimaryServiceOutput)
    {
        printlog(LOG_ERROR, "Failed to create primary service output, reason %s\n", OutputErrorStr);
        exit(1);
    }
    printlog(LOG_DEBUG, "PrimaryServiceOutput=%p\n", PrimaryServiceOutput);

    if (CommandInit())
    {
        printlog(LOG_ERROR, "CommandInit failed!\n");
        exit(1);
    }

    if (DaemonMode)
    {
        char serverNameBuffer[40];
        if (!serverName)
        {
            sprintf(serverNameBuffer, "DVBStreamer Adapter %d", adapterNumber);
            serverName = serverNameBuffer;
        }
        if (BinaryCommsInit(adapterNumber, serverName, username, password))
        {
            printlog(LOG_ERROR, "BinaryCommsInit failed!\n");
            exit(1);
        }
    }

    if (startupFile)
    {
        if (CommandProcessFile(startupFile))
        {
            printlog(LOG_ERROR, "%s not found!\n", startupFile);
        }
        free(startupFile);
    }
    printlog(LOG_DEBUGV, "Startup file processed\n");

    if (DaemonMode)
    {
        BinaryCommsAcceptConnections();
        printlog(LOG_DEBUGV, "Binary comms finished, shutting down\n");
    }
    else
    {
        CommandLoop();
        printlog(LOG_DEBUGV, "Command loop finished, shutting down\n");
    }

    BinaryCommsDeInit();

    CommandDeInit();

    OutputsDeInit();
    printlog(LOG_DEBUGV, "Outputs deinitialised\n");

    /* Disable all the filters */
    for (i = 0; i < PIDFilterIndex_Count; i ++)
    {
        PIDFilters[i]->enabled = FALSE;
    }
    printlog(LOG_DEBUGV, "PID filters stopped\n");

    PATProcessorDestroy( PIDFilters[PIDFilterIndex_PAT]);
    PMTProcessorDestroy( PIDFilters[PIDFilterIndex_PMT]);
    SDTProcessorDestroy( PIDFilters[PIDFilterIndex_SDT]);

    printlog(LOG_DEBUGV, "Processors destroyed\n");
    /* Close the adapter and shutdown the filter etc*/
    DVBDispose(DVBAdapter);
    printlog(LOG_DEBUGV, "DVB Adapter shutdown\n");

    TSFilterDestroy(TSFilter);
    printlog(LOG_DEBUGV, "TSFilter destroyed\n");

    UDPOutputClose(outputArg);
    printlog(LOG_DEBUGV, "UDPOutput closed\n");

    CacheDeInit();
    printlog(LOG_DEBUGV, "Cache deinitalised\n");

    DBaseDeInit();
    printlog(LOG_DEBUGV, "Database deinitalised\n");

    if (DaemonMode)
    {
        DeinitDaemon();
    }
    return 0;
}

/*
 * Find the service named <name> and tune to the new frequency for the multiplex the service is
 * on (if required) and then select the new service id to filter packets for.
 */
Service_t *SetCurrentService(char *name)
{
    Multiplex_t *multiplex;
    Service_t *service;

    TSFilterLock(TSFilter);
    printlog(LOG_DEBUG,"Writing changes back to database.\n");
    CacheWriteback();
    TSFilterUnLock(TSFilter);

    service = CacheServiceFindName(name, &multiplex);
    if (!service)
    {
        return NULL;
    }

    printlog(LOG_DEBUG, "Service found id:0x%04x Multiplex:%d\n", service->id, service->multiplexfreq);
    if ((CurrentService == NULL) || (!ServiceAreEqual(service,CurrentService)))
    {
        printlog(LOG_DEBUGV,"Disabling filters\n");
        TSFilterEnable(TSFilter, FALSE);

        if (CurrentMultiplex)
        {
            printlog(LOG_DEBUG,"Current Multiplex frequency = %d TS id = %d\n",CurrentMultiplex->freq, CurrentMultiplex->tsid);
        }
        else
        {
            printlog(LOG_DEBUG,"No current Multiplex!\n");
        }

        if (multiplex)
        {
            printlog(LOG_DEBUG,"New Multiplex frequency =%d TS id = %d\n",multiplex->freq, multiplex->tsid);
        }
        else
        {
            printlog(LOG_DEBUG,"No new Multiplex!\n");
        }

        if ((CurrentMultiplex!= NULL) && MultiplexAreEqual(multiplex, CurrentMultiplex))
        {
            printlog(LOG_DEBUGV,"Same multiplex\n");
            CurrentService = service;
        }
        else
        {
            struct dvb_frontend_parameters feparams;
            if (CurrentMultiplex)
            {
                free((void*)CurrentMultiplex);
            }

            printlog(LOG_DEBUGV,"Caching Services\n");
            CacheLoad(multiplex);
            CurrentMultiplex = multiplex;

            printlog(LOG_DEBUGV,"Getting Frondend parameters\n");
            MultiplexFrontendParametersGet((Multiplex_t*)CurrentMultiplex, &feparams);

            printlog(LOG_DEBUGV,"Tuning\n");
            DVBFrontEndTune(DVBAdapter, &feparams);

            CurrentService = CacheServiceFindId(service->id);
            ServiceFree(service);
        }

        TSFilterZeroStats(TSFilter);
        OutputSetService(PrimaryServiceOutput, CurrentService);

        printlog(LOG_DEBUGV,"Enabling filters\n");
        TSFilterEnable(TSFilter, TRUE);
    }

    return (Service_t*)CurrentService;
}

/*
 * Output command line usage and help.
 */
static void usage(char *appname)
{
    fprintf(stderr,"Usage:%s <options>\n"
            "      Options:\n"
            "      -v            : Increase the amount of debug output, can be used multiple\n"
            "                      times for more output\n"
            "      -V            : Print version information then exit\n"
            "      -o <host:port>: Output transport stream via UDP to the given host and port\n"
            "      -a <adapter>  : Use adapter number (ie /dev/dvb/adapter<adapter>/...)\n"
            "      -f <file>     : Run startup script file before starting the command prompt\n"
            "      -t <file>     : Terrestrial channels.conf file to import services and \n"
            "                      multiplexes from.\n"
            "      -s <file>     : Satellite channels.conf file to import services and \n"
            "                      multiplexes from.(EXPERIMENTAL)\n"
            "      -c <file>     : Cable channels.conf file to import services and \n"
            "                      multiplexes from.(EXPERIMENTAL)\n",
            appname
           );
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

static void installsighandler(void)
{
    rl_catch_signals = 0;
    signal(SIGTERM, sighandler);
    signal(SIGINT, sighandler);
    signal(SIGQUIT, sighandler);
}

static void sighandler(int signum)
{
    if (!DaemonMode)
    {
        switch (signum)
        {
            case SIGINT:
            case SIGQUIT:
                rl_free_line_state ();
            case SIGTERM:
                rl_cleanup_after_signal();
                fclose(rl_instream);
                break;
        }
    }
    ExitProgram = TRUE;
}

static void InitDaemon(int adapter)
{
    char logfile[PATH_MAX];
    /* Our process ID and Session ID */
    pid_t pid, sid;

    /* Fork off the parent process */
    pid = fork();
    if (pid < 0)
    {
        exit(1);
    }

    /* If we got a good PID, then
       we can exit the parent process. */
    if (pid > 0)
    {
        FILE *fp;
        sprintf(PidFile, "/var/run/dvbstreamer-%d.pid", adapter);
        fp = fopen(PidFile, "wt");
        if (fp)
        {
            fprintf(fp, "%d", pid);
            fclose(fp);
        }
        exit(0);
    }

    /* Create a new SID for the child process */
    sid = setsid();
    if (sid < 0)
    {
        /* Log the failure */
        exit(1);
    }

    /* Change the current working directory */
    if ((chdir("/")) < 0)
    {
        /* Log the failure */
        exit(1);
    }

    /* Close out the standard file descriptors */
    fclose(stdin);
    fclose(stdout);
    fclose(stderr);
    sprintf(logfile, "/var/log/dvbstreamer-%d.err.log", adapter);
    stderr = freopen(logfile, "at", stderr);
    sprintf(logfile, "/var/log/dvbstreamer-%d.out.log", adapter);
    stdout = freopen(logfile, "at", stdout);
    DaemonMode = TRUE;
}

static void DeinitDaemon(void)
{
    /* Remove pid file */
    unlink(PidFile);
    exit(0);
}
