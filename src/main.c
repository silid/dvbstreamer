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
#include <sys/stat.h>
#include <malloc.h>
#include <readline/readline.h>
#include <readline/history.h>


#include "parsezap.h"
#include "dbase.h"
#include "epgtypes.h"
#include "epgchannel.h"
#include "multiplexes.h"
#include "services.h"
#include "dvbadapter.h"
#include "ts.h"
#include "main.h"
#include "dispatchers.h"
#include "servicefilter.h"
#include "cache.h"
#include "logging.h"
#include "commands.h"
#include "remoteintf.h"
#include "deliverymethod.h"
#include "pluginmgr.h"
#include "plugin.h"
#include "tuning.h"
#include "atsctext.h"
#include "deferredproc.h"
#include "events.h"
#include "properties.h"

#include "standard/mpeg2.h"
#include "standard/dvb.h"
#include "standard/atsc.h"


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

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
/* External Command Prototypes. */
extern void CommandInstallInfo(void);
extern void CommandUnInstallInfo(void);

extern void CommandInstallServiceFilter(void);
extern void CommandUnInstallServiceFilter(void);

extern void CommandInstallScanning(void);
extern void CommandUnInstallScanning(void);

extern void CommandInstallEPG(void);
extern void CommandUnInstallEPG(void);

static void usage(char *appname);
static void version(void);
static void sighandler(int signum);
static void installsighandler(void);
static void InitDaemon(int adapter);
static void DeInitDaemon(void);

static void InstallSysProperties(void);
static int SysPropertyGetUptime(void *userArg, PropertyValue_t *value);
static int SysPropertyGetUptimeSecs(void *userArg, PropertyValue_t *value);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
volatile bool ExitProgram = FALSE;
bool DaemonMode = FALSE;

const char PrimaryService[] = "<Primary>";
char DataDirectory[PATH_MAX];

static char PidFile[PATH_MAX];
static TSReader_t *TSReader;
static DVBAdapter_t *DVBAdapter;
static ServiceFilter_t PrimaryServiceFilter;
static const char MAIN[] = "Main";
static time_t StartTime;
static char *versionStr = VERSION;
static char hexVersionStr[5]; /* XXXX\0 */

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
int main(int argc, char *argv[])
{
    char *startupFile = NULL;
    int adapterNumber = 0;
    int scanAll = 0;
    int logLevel = 0;
    char *username = "dvbstreamer";
    char *password = "control";
    char *serverName = NULL;
    char *bindAddress = NULL;
    char *primaryMRL = "null://";
    bool remoteInterface = FALSE;
    bool disableConsoleInput = FALSE;
    bool hwRestricted = FALSE;
    bool forceISDB = FALSE;
    LNBInfo_t lnbInfo;
    
    DeliveryMethodInstance_t *dmInstance;
    char logFilename[PATH_MAX] = {0};

    /* Create the data directory */
    sprintf(DataDirectory, "%s/.dvbstreamer", getenv("HOME"));
    mkdir(DataDirectory, S_IRWXU);

    installsighandler();

    while (!ExitProgram)
    {
        int c;
        c = getopt(argc, argv, "vVdDro:a:f:u:p:n:F:i:RL:I");
        if (c == -1)
        {
            break;
        }
        switch (c)
        {
                case 'v': logLevel ++;
                break;
                case 'L': strcpy(logFilename, optarg);
                break;
                case 'V':
                version();
                exit(0);
                break;
                case 'o': primaryMRL = optarg;
                break;
                case 'a': adapterNumber = atoi(optarg);

                break;
                case 'R':
#if defined(ENABLE_DVB)
                hwRestricted = TRUE;
#else
                fprintf(stderr, "Hardware restricted mode only supported for DVB!\n");
#endif
                break;
                case 'f': startupFile = optarg;
                break;
                /* Daemon options */
                case 'd': DaemonMode = TRUE;
                break;
                /* Remote Interface Options */
                case 'r': remoteInterface = TRUE;
                break;
                case 'D': disableConsoleInput = TRUE;
                remoteInterface = TRUE;
                break;
                case 'u': username = optarg;
                break;
                case 'p': password = optarg;
                break;
                case 'n': serverName = optarg;
                break;
                case 'i': bindAddress = optarg;
                break;
                case 'I': forceISDB = TRUE;
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


    if (logFilename[0])
    {
        if (DaemonMode && (strcmp(logFilename, "-") == 0))
        {
            fprintf(stderr, "Cannot use STDERR for logging when running as a daemon!");
            exit(1);
        }
        
        if (LoggingInitFile(logFilename, logLevel))
        {
            perror("Could not open user specified log file:");
            exit(1);
        }
    }
    else
    {
        sprintf(logFilename, "dvbstreamer-%d.log", adapterNumber);
        if (LoggingInit(logFilename, logLevel))
        {
            perror("Couldn't initialising logging module:");
            exit(1);
        }
    }

    sprintf(logFilename, "%s/levels-%d.log", DataDirectory, adapterNumber);
    LogLoadModuleLevels(logFilename);
    
    if (DaemonMode)
    {
        if (startupFile && (startupFile[0] != '/'))
        {
            char *cwd = getcwd(NULL, 0);
            if (asprintf(&startupFile, "%s/%s", cwd, startupFile) == -1)
            {
                LogModule(LOG_ERROR, MAIN, "Failed to allocate memory for startup file path!\n");
            }
            free(cwd);
        }
        InitDaemon(adapterNumber);
    }

    StartTime = time(NULL);


    LogRegisterThread(pthread_self(), "Main");
    LogModule(LOG_INFO, MAIN, "DVBStreamer starting");
    LogModule(LOG_INFOV, MAIN, "Using adapter %d\n", adapterNumber);
    if (startupFile)
    {
        LogModule(LOG_INFOV, MAIN, "Using startup script %s\n", startupFile);
    }


    if (primaryMRL == NULL)
    {
        LogModule(LOG_ERROR, MAIN, "No output set!\n");
        usage(argv[0]);
        exit(1);
    }

    INIT(ObjectInit(), "objects");
    INIT(EventsInit(), "events");
    INIT(PropertiesInit(), "properties");
    INIT(DBaseInit(adapterNumber), "database");
    INIT(EPGTypesInit(), "EPG types");
    INIT(EPGChannelInit(), "EPG channel");
    INIT(MultiplexInit(), "multiplex");
    INIT(ServiceInit(), "service");
    INIT(DispatchersInit(), "dispatchers");
    INIT(CacheInit(), "cache");
    INIT(DeliveryMethodManagerInit(), "delivery method manager");
    INIT(DeferredProcessingInit(), "deferred processing");

    LogModule(LOG_INFO, MAIN, "%d Services available on %d Multiplexes\n", ServiceCount(), 
                                                                           MultiplexCount());

    /* Initialise the DVB adapter */
    DVBAdapter = DVBInit(adapterNumber, hwRestricted, forceISDB);
    if (!DVBAdapter)
    {
        printf("Could not open dvb adapter %d!\n", adapterNumber);
        exit(1);
    }

#if defined(ENABLE_DVB)
    {
        int i;
        DVBSupportedDeliverySys_t *supportedSystems = DVBFrontEndGetDeliverySystems(DVBAdapter);
        for (i = 0; i < supportedSystems->nrofSystems; i ++)
        {
            if ((supportedSystems->systems[i] == DELSYS_DVBS) || (supportedSystems->systems[i] == DELSYS_DVBS2))
            {
                char *lnb;
                if (DBaseMetadataGet(METADATA_NAME_LNB, &lnb))
                {
                    memset(&lnbInfo, 0, sizeof(lnbInfo));
                    DBaseMetadataGetInt(METADATA_NAME_LNB_LOW_FREQ, (int*)&lnbInfo.lowFrequency);
                    DBaseMetadataGetInt(METADATA_NAME_LNB_HIGH_FREQ, (int*)&lnbInfo.highFrequency);
                    DBaseMetadataGetInt(METADATA_NAME_LNB_SWITCH_FREQ, (int*)&lnbInfo.switchFrequency);
                }
                else
                {
                    LNBDecode(lnb, &lnbInfo);
                    free(lnb);
                }
                DVBFrontEndLNBInfoSet(DVBAdapter, &lnbInfo);
                break;
            }
        }
    }
#endif

    /* Create Transport stream filter thread */
    INIT(!(TSReader = TSReaderCreate(DVBAdapter)), "TS reader");

    if (MainIsDVB())
    {
#if defined(ENABLE_DVB)
        LogModule(LOG_INFO, MAIN, "Starting DVB filters\n");
        INIT(DVBStandardInit(TSReader), "DVB Filters");
#endif
    }
    
    if (MainIsATSC())
    {
#if defined(ENABLE_ATSC)
        LogModule(LOG_INFO, MAIN, "Starting ATSC filters\n");
        INIT(ATSCStandardInit(TSReader), "ATSC Filters");
#endif
    }
    
    if (MainIsISDB())
    {
        LogModule(LOG_INFO, MAIN, "Starting ISDB filters\n");
        INIT(MPEG2StandardInit(TSReader), "ISDB Filters");
    }
    
    INIT(ServiceFilterInit(), "service filter");
    INIT(CommandInit(), "commands");

    /* Install commands */
    CommandInstallServiceFilter();
    CommandInstallInfo();
    CommandInstallScanning();
    CommandInstallEPG();

    INIT(TuningInit(), "tuning");

    InstallSysProperties();

    /*
     * Start plugins after outputs but before creating the primary output to
     * allow pugins to create outputs and allow new delivery methods to be
     *  registered.
     */
    INIT(PluginManagerInit(), "plugin manager");

    /* Create Service filter */
    PrimaryServiceFilter = ServiceFilterCreate(TSReader, (char *)PrimaryService);
    if (!PrimaryServiceFilter)
    {
        LogModule(LOG_ERROR, MAIN, "Failed to create primary service filter\n");
        exit(1);
    }
    dmInstance = DeliveryMethodCreate(primaryMRL);
    if (dmInstance == NULL)
    {
        if (strcmp(primaryMRL, "null://"))
        {
            printf("Failed to create delivery method for mrl (%s) falling back to null://\n", primaryMRL);
            dmInstance = DeliveryMethodCreate("null://");
        }
        if (dmInstance == NULL)
        {
            fprintf(stderr,
                "Failed to create fallback (null://) delivery method\n"
                "Check that you have installed dvbstreamer plugins to the correct place!\n"
                "Plugin path: %s\n", DVBSTREAMER_PLUGINDIR);
            exit(1);
        }
    }

    ServiceFilterDeliveryMethodSet(PrimaryServiceFilter, dmInstance);

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

    if (DBaseMetadataGetInt(METADATA_NAME_SCAN_ALL, &scanAll) == 0)
    {
        if (scanAll && !forceISDB)
        {
            printf("New setup, performing initial scan to fill in missing details.\n");
            if (!CommandExecuteConsole("scan all"))
            {
                printf("Failed to find scan command\n");
            }
            printf("Initial scan finished.\n");
        }
        DBaseMetadataDelete(METADATA_NAME_SCAN_ALL);
    }

    LogModule(LOG_INFO, MAIN, "DVBStreamer ready.");

    if (DaemonMode)
    {
        DispatchersStart(TRUE);
        LogModule(LOG_DEBUGV, MAIN, "Remote interface finished, shutting down\n");
        RemoteInterfaceDeInit();
    }
    else
    {
        if (disableConsoleInput)
        {
            DispatchersStart(TRUE);
        }
        else
        {
            DispatchersStart(FALSE);
            CommandLoop();
            LogModule(LOG_DEBUGV, MAIN, "Command loop finished, shutting down\n");
            ExitProgram = TRUE;
        }

        if (remoteInterface)
        {
            RemoteInterfaceDeInit();
        }
    }
    DispatchersStop();
    TSReaderEnable(TSReader, FALSE);

    ServiceFilterDestroyAll(TSReader);

    /* Stop the deferred processing as when we unload the plugins we may be
     * unloading code that is required by any jobs left on the queue
     */
    DEINIT(DeferredProcessingDeinit(), "deferred processing");

    /* Destroy all delivery method instances before shutting down the plugins.
     * We do this as although there may be instances being used by plugins,
     * we do not know the order the plugins will be shutdown. This may mean the
     * delivery method plugin is shutdown before the plugin using the instance!
     */
    DeliveryMethodDestroyAll();

    DEINIT(PluginManagerDeInit(), "plugin manager");

    DEINIT(DeliveryMethodManagerDeInit(), "delivery method manager");

    DEINIT(TuningDeInit(), "tuning");

    /* Uninstall commands */
    CommandUnInstallEPG();
    CommandUnInstallServiceFilter();
    CommandUnInstallInfo();
    CommandUnInstallScanning();
    
    DEINIT(CommandDeInit(), "commands");
    DEINIT(ServiceFilterDeInit(), "service filter");

    if (MainIsDVB())
    {
#if defined(ENABLE_DVB)
        DVBStandardDeinit(TSReader);
#endif
    }

    if (MainIsATSC())
    {
#if defined(ENABLE_ATSC)
        ATSCStandardDeinit(TSReader);
#endif
    }
    
    if (MainIsISDB())
    {
        MPEG2StandardDeinit(TSReader);
    }

    LogModule(LOG_DEBUGV, MAIN, "Processors destroyed\n");
    /* Close the adapter and shutdown the filter etc*/
    DEINIT(TSReaderDestroy(TSReader), "TS filter");

    DVBFrontEndLNBInfoGet(DVBAdapter, &lnbInfo);

    if (lnbInfo.name)
    {
        DBaseMetadataSet(METADATA_NAME_LNB, lnbInfo.name);
    }
    else
    {
        DBaseMetadataDelete(METADATA_NAME_LNB);
        DBaseMetadataSetInt(METADATA_NAME_LNB_LOW_FREQ, (int)lnbInfo.lowFrequency);
        DBaseMetadataSetInt(METADATA_NAME_LNB_HIGH_FREQ, (int)lnbInfo.highFrequency);
        DBaseMetadataSetInt(METADATA_NAME_LNB_SWITCH_FREQ, (int)lnbInfo.switchFrequency);        
    }
        
    
    DEINIT(DVBDispose(DVBAdapter), "DVB adapter");

    DEINIT(CacheDeInit(), "cache");
    DEINIT(DispatchersDeInit(), "dispatchers");
    DEINIT(ServiceDeInit(), "service");
    DEINIT(MultiplexDeInit(), "multiplex");
    DEINIT(EPGChannelDeInit(), "EPG channel");
    DEINIT(EPGTypesDeInit(), "EPG types");
    DEINIT(DBaseDeInit(), "database");
    DEINIT(PropertiesDeInit(), "properties");
    DEINIT(EventsDeInit(), "events");
    DEINIT(ObjectDeinit(), "objects");

    if (DaemonMode)
    {
        DeInitDaemon();
    }
    
    LogModule(LOG_INFO, MAIN, "DVBStreamer finished.");
    LoggingDeInit();
    return 0;
}

void UpdateDatabase()
{
    TSReaderLock(TSReader);
    CacheWriteback();
    TSReaderUnLock(TSReader);
}

static void InstallSysProperties(void)
{
    sprintf(hexVersionStr, "%02x%02x", DVBSTREAMER_MAJOR, DVBSTREAMER_MINOR);
    PropertiesAddSimpleProperty("sys", "version", "Version of this instance of DVBStreamer", PropertyType_String,
                        &versionStr, SIMPLEPROPERTY_R);
    PropertiesAddSimpleProperty("sys", "hexversion", "Version of this instance of DVBStreamer as a 16 bit hex number", PropertyType_String,
                        &hexVersionStr, SIMPLEPROPERTY_R);
    PropertiesAddProperty("sys", "uptime", "The time that this instance has been running in days/hours/minutes/seconds.",
                      PropertyType_String, NULL, SysPropertyGetUptime, NULL);
    PropertiesAddProperty("sys.uptime", "seconds", "The time that this instance has been running in seconds.",
                          PropertyType_Int, NULL, SysPropertyGetUptimeSecs, NULL);
}

static int SysPropertyGetUptime(void *userArg, PropertyValue_t *value)
{
    char *uptimeStr = NULL;
    time_t now;
    int seconds;
    int d, h, m, s;
    time(&now);
    seconds = (int)difftime(now, StartTime);
    d = seconds / (24 * 60 * 60);
    h = (seconds - (d * 24 * 60 * 60)) / (60 * 60);
    m = (seconds - ((d * 24 * 60 * 60) + (h * 60 * 60))) / 60;
    s = (seconds - ((d * 24 * 60 * 60) + (h * 60 * 60) + (m * 60)));

    if (asprintf(&uptimeStr, "%d Days %d Hours %d Minutes %d seconds", d, h, m, s) == -1)
    {
        LogModule(LOG_INFO, MAIN, "Failed to allocate memory for uptime string.\n");
    }
    value->u.string = uptimeStr;
    return 0;
}

static int SysPropertyGetUptimeSecs(void *userArg, PropertyValue_t *value)
{
    time_t now;
    time(&now);
    value->u.integer = (int)difftime(now, StartTime);
    return 0;
}


TSReader_t *MainTSReaderGet(void)
{
    return TSReader;
}

DVBAdapter_t *MainDVBAdapterGet(void)
{
    return DVBAdapter;
}

ServiceFilter_t MainServiceFilterGetPrimary(void)
{
    return PrimaryServiceFilter;
}

bool MainIsDVB()
{
#if defined(ENABLE_DVB) && defined(ENABLE_ATSC)
    int i;
    DVBSupportedDeliverySys_t *supportedSystems = DVBFrontEndGetDeliverySystems(DVBAdapter);
    for ( i = 0; i < supportedSystems->nrofSystems; i ++)
    {
        if ((supportedSystems->systems[i] == DELSYS_DVBS) ||
            (supportedSystems->systems[i] == DELSYS_DVBC) ||
            (supportedSystems->systems[i] == DELSYS_DVBT)
            )
        {
            return TRUE;
        }
    }
    return FALSE;    
#elif defined(ENABLE_DVB)

    return TRUE;

#elif defined(ENABLE_ATSC)

    return FALSE;

#else

#error Either ENABLE_DVB or ENABLE_ATSC needs to be defined!

#endif
}

bool MainIsATSC()
{
#if defined(ENABLE_DVB) && defined(ENABLE_ATSC)
    int i;
    DVBSupportedDeliverySys_t *supportedSystems = DVBFrontEndGetDeliverySystems(DVBAdapter);
    for ( i = 0; i < supportedSystems->nrofSystems; i ++)
    {
        if (supportedSystems->systems[i] == DELSYS_ATSC)
        {
            return TRUE;
        }
    }
    return FALSE;    
#elif defined(ENABLE_ATSC)
    return TRUE;
#else    
    return FALSE;
#endif
}

bool MainIsISDB()
{
    int i;
    DVBSupportedDeliverySys_t *supportedSystems = DVBFrontEndGetDeliverySystems(DVBAdapter);
    for ( i = 0; i < supportedSystems->nrofSystems; i ++)
    {
        if (supportedSystems->systems[i] == DELSYS_ISDBT)
        {
            return TRUE;
        }
    }
    return FALSE;    
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
            "      -L <file>     : Set the location of the log file.\n"
            "      -V            : Print version information then exit\n"
            "      -o <mrl>      : Output primary service to the specified mrl.\n"
            "      -a <adapter>  : Use adapter number (ie /dev/dvb/adapter<adapter>/...)\n"
            "      -f <file>     : Run startup script file before starting the command prompt\n"
            "      -d            : Run as a daemon.\n"
            "      -R            : Use hardware PID filters, only 1 service filter supported.\n"
            "      -I            : Force use of ISDB-T delivery system\n"
            "\n"
            "      Remote Interface Options\n"
            "      -r            : Start remote interface as well as console shell.\n"
            "      -D            : Start remote interface but disable console shell.\n"
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
    signal(SIGPIPE, SIG_IGN);
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
                if (rl_instream)
                {
                    fclose(rl_instream);
                }
                break;
        }
    }
    LogModule(LOG_DEBUG, MAIN, "Got signal %d exiting\n", signum);
    ExitProgram = TRUE;
    DispatchersExitLoop();
}

/*******************************************************************************
* Daemon functions                                                             *
*******************************************************************************/
static void InitDaemon(int adapter)
{
    /* Our process ID and Session ID */
    pid_t pid, sid;

    /* Fork off the parent process */
    pid = fork();
    if (pid < 0)
    {
        LogModule(LOG_ERROR, MAIN, "First fork failed while going into daemon mode");
        exit(1);
    }

    /* If we got a good PID, then
       we can exit the parent process. */
    if (pid > 0)
    {
        exit(0);
    }

    /* Create a new SID for the child process */
    sid = setsid();
    if (sid < 0)
    {
        LogModule(LOG_ERROR, MAIN, "setsid failed while going into daemon mode");
        /* Log the failure */
        exit(1);
    }

    /* Change the current working directory */
    if ((chdir("/")) < 0)
    {
        LogModule(LOG_ERROR, MAIN, "chdir failed while going into daemon mode");
        /* Log the failure */
        exit(1);
    }

    /* Redirect standard files to logging file */
    if (freopen( "/dev/null", "r", stdin) == NULL)
    {
        LogModule(LOG_ERROR, MAIN, "Failed to redirect stdin to /dev/null!");
    }

    LoggingRedirectStdErrStdOut();
    
    pid = fork();
    if (pid < 0)
    {
        LogModule(LOG_ERROR, MAIN, "Second fork failed while going into daemon mode");
        exit(1);
    }

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
    
    DaemonMode = TRUE;
}

static void DeInitDaemon(void)
{

    /* Remove pid file */
    unlink(PidFile);
    exit(0);
}

