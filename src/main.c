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
#define _GNU_SOURCE
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
#include <sys/stat.h>
#include <malloc.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "parsezap.h"
#include "dbase.h"
#include "epgdbase.h"
#include "multiplexes.h"
#include "services.h"
#include "dvb.h"
#include "ts.h"
#include "main.h"
#include "patprocessor.h"
#include "pmtprocessor.h"
#include "sdtprocessor.h"
#include "nitprocessor.h"
#include "tdtprocessor.h"
#include "psipprocessor.h"
#include "sectionprocessor.h"
#include "pesprocessor.h"
#include "servicefilter.h"
#include "cache.h"
#include "logging.h"
#include "commands.h"
#include "outputs.h"
#include "remoteintf.h"
#include "deliverymethod.h"
#include "pluginmgr.h"
#include "plugin.h"
#include "tuning.h"

/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/

#define INIT(_func, _name) \
    do {\
        if (_func) \
        { \
            LogModule(LOG_ERROR, MAIN, "Failed to initialise %s.\n", _name); \
            if (DaemonMode)\
            {\
                unlink(PidFile);\
            }\
            exit(1);\
        }\
        LogModule(LOG_DEBUGV, MAIN, "Initialised %s.\n", _name);\
    }while(0)

#define DEINIT(_func, _name) \
    do {\
        _func;\
        LogModule(LOG_DEBUGV, MAIN, "Deinitialised %s\n", _name);\
    }while(0)

#define MAX_PIDFILTERS 5 /* For DVB this is PAT,PMT,SDT,TDT,NIT for ATSC this is just PAT,PMT,PSIP */

#define PIDFILTER_INDEX_PAT 0
#define PIDFILTER_INDEX_PMT 1
/* DVB */
#define PIDFILTER_INDEX_SDT 2
#define PIDFILTER_INDEX_NIT 3
#define PIDFILTER_INDEX_TDT 4
/* ATSC */
#define PIDFILTER_INDEX_PSIP 2

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void usage(char *appname);
static void version(void);
static void sighandler(int signum);
static void installsighandler(void);
static void InitDaemon(int adapter);
static void DeinitDaemon(void);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
TSFilter_t *TSFilter;
DVBAdapter_t *DVBAdapter;

bool ExitProgram = FALSE;
bool DaemonMode = FALSE;
bool FilterServiceNames = FALSE;
char FilterReplacementChar = ' ';
char PrimaryService[] = "<Primary>";

char DataDirectory[PATH_MAX];

static char PidFile[PATH_MAX];

static char MAIN[] = "Main";

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
int main(int argc, char *argv[])
{
    char *startupFile = NULL;
    int i;
    int adapterNumber = 0;
    char *username = "dvbstreamer";
    char *password = "control";
    char *serverName = NULL;
    char *bindAddress = NULL;
    char *primaryOutput = "null://";
    bool remoteInterface = FALSE;
    Output_t *primaryServiceOutput = NULL;
    PIDFilter_t *PIDFilters[MAX_PIDFILTERS];
    
    /* Create the data directory */
    sprintf(DataDirectory, "%s/.dvbstreamer", getenv("HOME"));
    mkdir(DataDirectory, S_IRWXU);

    installsighandler();

    while (!ExitProgram)
    {
        int c;
        c = getopt(argc, argv, "vVdro:a:f:u:p:n:F:i:");
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
                case 'o': primaryOutput = optarg;
                break;
                case 'a': adapterNumber = atoi(optarg);
                LogModule(LOG_INFOV, MAIN, "Using adapter %d\n", adapterNumber);
                break;
                case 'f': startupFile = optarg;
                LogModule(LOG_INFOV, MAIN, "Using startup script %s\n", startupFile);
                break;
                case 'F':
                FilterServiceNames = TRUE;
                FilterReplacementChar = optarg[0];
                LogModule(LOG_INFOV, MAIN, "Non-printable characters will be replaced with \'%c\'\n", FilterReplacementChar);
                break;
                /* Daemon options */
                case 'd': DaemonMode = TRUE;
                break;
                /* Remote Interface Options */
                case 'r': remoteInterface = TRUE;
                break;
                case 'u': username = optarg;
                break;
                case 'p': password = optarg;
                break;
                case 'n': serverName = optarg;
                break;
                case 'i': bindAddress = optarg;
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

    INIT(ObjectInit(), "objects");

    if (DaemonMode)
    {
        if (startupFile && (startupFile[0] != '/'))
        {
            char *cwd = getcwd(NULL, 0);
            asprintf(&startupFile, "%s/%s", cwd, startupFile);
            free(cwd);
        }
        InitDaemon( adapterNumber);
    }

    if (primaryOutput == NULL)
    {
        LogModule(LOG_ERROR, MAIN, "No output set!\n");
        usage(argv[0]);
        exit(1);
    }

    INIT(DBaseInit(adapterNumber), "database");
    INIT(EPGDBaseInit(adapterNumber), "EPG database");
    INIT(MultiplexInit(), "multiplex");
    INIT(ServiceInit(), "service");
    INIT(CacheInit(), "cache");
    INIT(DeliveryMethodManagerInit(), "delivery method manager");

    LogModule(LOG_INFO, MAIN, "%d Services available on %d Multiplexes\n", ServiceCount(), MultiplexCount());

    /* Initialise the DVB adapter */
    INIT(!(DVBAdapter = DVBInit(adapterNumber)), "DVB adapter");

    if (DVBAdapter->info.type == FE_QPSK)
    {
        int lowFreq = 0;
        int highFreq = 0;
        int switchFreq = 0;
        
        DBaseMetadataGetInt(METADATA_NAME_LNB_LOW_FREQ, &lowFreq);
        DBaseMetadataGetInt(METADATA_NAME_LNB_HIGH_FREQ, &highFreq);
        DBaseMetadataGetInt(METADATA_NAME_LNB_SWITCH_FREQ, &switchFreq);
        DVBFrontEndLNBInfoSet(DVBAdapter, lowFreq, highFreq, switchFreq);
    }
    
    /* Create Transport stream filter thread */
    INIT(!(TSFilter = TSFilterCreate(DVBAdapter)), "TS filter");

    /* Create PAT/PMT/SDT filters */
    memset(&PIDFilters, 0, sizeof(PIDFilters));
    
    PIDFilters[PIDFILTER_INDEX_PAT] = PATProcessorCreate(TSFilter);
    PIDFilters[PIDFILTER_INDEX_PMT] = PMTProcessorCreate(TSFilter);
    if (MainIsDVB())
    {
        LogModule(LOG_INFO, MAIN, "Starting DVB filters\n");        
        PIDFilters[PIDFILTER_INDEX_SDT] = SDTProcessorCreate(TSFilter);
        PIDFilters[PIDFILTER_INDEX_NIT] = NITProcessorCreate(TSFilter);
        PIDFilters[PIDFILTER_INDEX_TDT] = TDTProcessorCreate(TSFilter);
    }
    else
    {
        LogModule(LOG_INFO, MAIN, "Starting ATSC filters\n");        
        PIDFilters[PIDFILTER_INDEX_PSIP] = PSIPProcessorCreate(TSFilter);
    }

    /* Enable all the filters */
    for (i = 0; i < MAX_PIDFILTERS; i ++)
    {
        if (PIDFilters[i])
        {
            PIDFilters[i]->enabled = TRUE;
        }
    }
    LogModule(LOG_DEBUGV, MAIN, "PID filters started\n");

    INIT(OutputsInit(), "outputs");
    INIT(CommandInit(), "commands");

    INIT(TuningInit(), "tuning");

    /*
     * Start plugins after outputs but before creating the primary output to
     * allow pugins to create outputs and allow new delivery methods to be
     *  registered.
     */
    INIT(PluginManagerInit(), "plugin manager");

    /* Create Service filter */
    primaryServiceOutput = OutputAllocate(PrimaryService, OutputType_Service, primaryOutput);
    if (!primaryServiceOutput)
    {
        LogModule(LOG_ERROR, MAIN, "Failed to create primary service output, reason %s\n", OutputErrorStr);
        exit(1);
    }

    if (DaemonMode || remoteInterface)
    {
        char serverNameBuffer[40];
        if (!serverName)
        {
            sprintf(serverNameBuffer, "DVBStreamer Adapter %d", adapterNumber);
            serverName = serverNameBuffer;
        }
        INIT(RemoteInterfaceInit(adapterNumber, serverName, bindAddress, username, password), "remote interface");
    }

    if (startupFile)
    {
        if (CommandProcessFile(startupFile))
        {
            LogModule(LOG_ERROR, MAIN, "%s not found!\n", startupFile);
        }
        LogModule(LOG_DEBUGV, MAIN, "Startup file processed\n");
    }

    if (DaemonMode)
    {
        RemoteInterfaceAcceptConnections();
        LogModule(LOG_DEBUGV, MAIN, "Remote interface finished, shutting down\n");
        RemoteInterfaceDeInit();
    }
    else
    {
        if (remoteInterface)
        {
            RemoteInterfaceAsyncAcceptConnections();
        }

        CommandLoop();
        LogModule(LOG_DEBUGV, MAIN, "Command loop finished, shutting down\n");

        if (remoteInterface)
        {
            RemoteInterfaceDeInit();
        }
    }

    DEINIT(OutputsDeInit(), "outputs");

    /*
     * Deinit Plugins after outputs so all delivery methods are properly torn
     * down.
     */
    DEINIT(PluginManagerDeInit(), "plugin manager");

    DEINIT(TuningDeinit(), "tuning");

    DEINIT(CommandDeInit(), "commands");

    /* Disable all the filters */
    for (i = 0; i < MAX_PIDFILTERS; i ++)
    {
        if (PIDFilters[i])
        {
            PIDFilters[i]->enabled = FALSE;
        }
    }
    LogModule(LOG_DEBUGV, MAIN, "PID filters stopped\n");

    PATProcessorDestroy( PIDFilters[PIDFILTER_INDEX_PAT]);
    PMTProcessorDestroy( PIDFilters[PIDFILTER_INDEX_PMT]);
    if (MainIsDVB())
    {
        SDTProcessorDestroy( PIDFilters[PIDFILTER_INDEX_SDT]);
        NITProcessorDestroy( PIDFilters[PIDFILTER_INDEX_NIT]);
        TDTProcessorDestroy( PIDFilters[PIDFILTER_INDEX_TDT]);
    }
    else
    {
        PSIPProcessorDestroy( PIDFilters[PIDFILTER_INDEX_PSIP]);
    }
    SectionProcessorDestroyAllProcessors();
    PESProcessorDestroyAllProcessors();
    
    LogModule(LOG_DEBUGV, MAIN, "Processors destroyed\n");
    /* Close the adapter and shutdown the filter etc*/
    DEINIT(TSFilterDestroy(TSFilter), "TS filter");
    DEINIT(DVBDispose(DVBAdapter), "DVB adapter");
    DEINIT(DeliveryMethodManagerDeInit(), "delivery method manager");
    DEINIT(CacheDeInit(), "cache");    

    DEINIT(ServiceDeinit(), "service");
    DEINIT(MultiplexDeinit(), "multiplex");
    DEINIT(EPGDBaseDeInit(), "EPG database");    
    DEINIT(DBaseDeInit(), "database");

    if (DaemonMode)
    {
        DeinitDaemon();
    }

    DEINIT(ObjectDeinit(), "objects");
    return 0;
}

void UpdateDatabase()
{
    TSFilterLock(TSFilter);
    CacheWriteback();
    TSFilterUnLock(TSFilter);
}

TSFilter_t *MainTSFilterGet(void)
{
    return TSFilter;
}

DVBAdapter_t *MainDVBAdapterGet(void)
{
    return DVBAdapter;
}

bool MainIsDVB()
{
    return DVBAdapter->info.type != FE_ATSC;
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
            "      -d            : Run as a daemon.\n"
            "      -F <character>: Replace unprintable (non-ASCII) characters with <character>\n"
            "\n"
            "      Remote Interface Options\n"
            "      -r            : Start remote interface as well as console (N/A when a daemon)\n"
            "      -u <username> : Username used to login remotely to control this instance.\n"
            "      -p <password> : Password used to login remotely to control this instance.\n"
            "      -n <name>     : Informational name for this instance.\n"
            "      -i <address>  : IP address to bind to.\n",
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

/*******************************************************************************
* Signal Handling functions                                                   *
*******************************************************************************/
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

/*******************************************************************************
* Daemon functions                                                             *
*******************************************************************************/
static void InitDaemon(int adapter)
{
    char *logdir = "/var/log";
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
        if (!fp)
        {
            sprintf(PidFile, "%s/dvbstreamer-%d.pid", DataDirectory, adapter);
            fp = fopen(PidFile, "wt");
        }

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
        LogModule(LOG_ERROR, MAIN, "setsid failed\n");
        /* Log the failure */
        exit(1);
    }

    /* Change the current working directory */
    if ((chdir("/")) < 0)
    {
        LogModule(LOG_ERROR, MAIN, "chdir failed\n");
        /* Log the failure */
        exit(1);
    }

    /* Close out the standard file descriptors */
    fclose(stdin);
    fclose(stdout);
    fclose(stderr);

    sprintf(logfile, "%s/dvbstreamer-%d.err.log", logdir, adapter);
    if (!freopen(logfile, "at", stderr))
    {
        logdir = DataDirectory;
        sprintf(logfile, "%s/dvbstreamer-%d.err.log", DataDirectory, adapter);
        freopen(logfile, "at", stderr);
    }
    sprintf(logfile, "%s/dvbstreamer-%d.out.log",logdir, adapter);
    freopen(logfile, "at", stdout);
    DaemonMode = TRUE;
}

static void DeinitDaemon(void)
{
    /* Remove pid file */
    unlink(PidFile);
    exit(0);
}

