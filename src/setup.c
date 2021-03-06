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

setup.c

Setups the database for the main application.

*/

#include "config.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sys/unistd.h>
#include <sys/stat.h>

#include "parsezap.h"
#include "dbase.h"
#include "multiplexes.h"
#include "services.h"
#include "main.h"
#include "logging.h"
#include "events.h"
#include "lnb.h"

/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/

#define INIT(_func, _name) \
    do {\
        if (_func) \
        { \
            LogModule(LOG_ERROR, SETUP, "Failed to initialise %s.\n", _name); \
            exit(1);\
        }\
        LogModule(LOG_DEBUGV, SETUP, "Initialised %s.\n", _name);\
    }while(0)

#define DEINIT(_func, _name) \
    do {\
        _func;\
        LogModule(LOG_DEBUGV, SETUP, "Deinitialised %s\n", _name);\
    }while(0)

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/

static void usage(char *appname);
static void version(void);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static const char SETUP[] = "Setup";

char DataDirectory[PATH_MAX];

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/

int main(int argc, char *argv[])
{
    DVBDeliverySystem_e channelsFileType = DELSYS_DVBT;
    char *channelsFile = NULL;
    int adapterNumber = 0;
#if defined(ENABLE_DVB)
    LNBInfo_t lnbInfo = {NULL,NULL,0,0,0};
#endif
    int rc;
    int logLevel = 0;
    char logFilename[PATH_MAX] = {0};

    /* Create the data directory */
    sprintf(DataDirectory, "%s/.dvbstreamer", getenv("HOME"));
    mkdir(DataDirectory, S_IRWXU);

    while (TRUE)
    {
        int c;
        c = getopt(argc, argv, "vVdro:a:t:s:S:c:A:l:hL:i:");
        if (c == -1)
        {
            break;
        }
        switch (c)
        {
            case 'v':
                logLevel++;
                break;
            case 'L': strcpy(logFilename, optarg);
                break;
            case 'V':
                version();
                exit(0);
                break;
            case 'a':
                adapterNumber = atoi(optarg);
                break;
                /* Database initialisation options*/
#if defined(ENABLE_DVB)
            case 't':
                channelsFile = optarg;
                channelsFileType = DELSYS_DVBT;
                break;
            case 's':
                channelsFile = optarg;
                channelsFileType = DELSYS_DVBS;
                break;
            case 'S':
                channelsFile = optarg;
                channelsFileType = DELSYS_DVBS2;
                break;
            case 'c':
                channelsFile = optarg;
                channelsFileType = DELSYS_DVBC;
                break;
#endif

#if defined(ENABLE_ATSC)
            case 'A':
                channelsFile = optarg;
                channelsFileType = DELSYS_ATSC;
                break;
#endif
            case 'i':
                channelsFile = optarg;
                channelsFileType = DELSYS_ISDBT;
                break;
                
#if defined(ENABLE_DVB)
            case 'l': /* LNB settings */
                if (LNBDecode(optarg, &lnbInfo))
                {
                    int i = 0;
                    LNBInfo_t *knownLNB;
                    do
                    {
                        knownLNB = LNBEnumerate(i);
                        if (knownLNB)
                        {
                            char **desclines;
                            printf("%s :\n", knownLNB->name);

                            for (desclines = knownLNB->desc; *desclines; desclines ++)
                            {
                                printf("   %s\n", *desclines);
                            }
                            printf("\n");
                            i ++;
                        }
                    }while(knownLNB);
                    exit(1);
                }
                break;
#endif
            case 'h':
            default:
                usage(argv[0]);
                exit(1);
        }
    }

    if (logFilename[0])
    {
        if (LoggingInitFile(logFilename, logLevel))
        {
            perror("Could not open user specified log file:");
            exit(1);
        }
    }
    else
    {
        sprintf(logFilename, "setupdvbstreamer-%d.log", adapterNumber);
        if (LoggingInit(logFilename, logLevel))
        {
            perror("Couldn't initialising logging module:");
            exit(1);
        }
    }


#if defined(ENABLE_DVB)
    if (((channelsFileType == DELSYS_DVBS) || (channelsFileType == DELSYS_DVBS2)) && (lnbInfo.lowFrequency == 0))
    {
        fprintf(stderr, "No LNB information provide for DVB-S channels.conf file!\n");
        exit(1);
    }
#endif

    INIT(ObjectInit(), "objects");
    INIT(EventsInit(), "events");
    INIT(DBaseInit(adapterNumber), "database");
    INIT(MultiplexInit(), "multiplex");
    INIT(ServiceInit(), "service");

    if (!channelsFile)
    {
        usage(argv[0]);
        exit(1);
    }
    rc = DBaseTransactionBegin();
    if (rc != SQLITE_OK)
    {
        LogModule(LOG_ERROR, SETUP, "Begin Transaction failed (%d:%s)\n", rc, sqlite3_errmsg(DBaseConnectionGet()));
    }

    LogModule(LOG_INFO, SETUP, "Importing services from %s\n", channelsFile);
    if (!parsezapfile(channelsFile, channelsFileType))
    {
        exit(1);
    }

#if defined(ENABLE_DVB)
    if ((channelsFileType == DELSYS_DVBS) || (channelsFileType == DELSYS_DVBS2))
    {
        /* Write out LNB settings. */
        DBaseMetadataSetInt(METADATA_NAME_LNB_LOW_FREQ, lnbInfo.lowFrequency);
        DBaseMetadataSetInt(METADATA_NAME_LNB_HIGH_FREQ, lnbInfo.highFrequency);
        DBaseMetadataSetInt(METADATA_NAME_LNB_SWITCH_FREQ, lnbInfo.switchFrequency);
    }
#endif

    DBaseMetadataSetInt(METADATA_NAME_SCAN_ALL, 1);
    rc = 0;
    rc = DBaseTransactionCommit();
    if (rc != SQLITE_OK)
    {
        LogModule(LOG_ERROR, SETUP, "Begin Transaction failed (%d:%s)\n", rc, sqlite3_errmsg(DBaseConnectionGet()));
    }

    printf("%d Services available on %d Multiplexes\n", ServiceCount(), MultiplexCount());

    DEINIT(ServiceDeInit(), "service");
    DEINIT(MultiplexDeInit(), "multiplex");
    DEINIT(DBaseDeInit(), "database");
    DEINIT(EventsDeInit(), "events");
    DEINIT(ObjectDeinit(), "objects");
    LoggingDeInit();
    return 0;
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
            "\n"
            "      -a <adapter>  : Use adapter number (ie /dev/dvb/adapter<adapter>/...)\n"
            "\n"
#if defined(ENABLE_DVB)
            "      -t <file>     : Terrestrial channels.conf file to import services and \n"
            "                      multiplexes from. (DVB-T)\n"
            "\n"
            "      -s <file>     : Satellite channels.conf file to import services and \n"
            "                      multiplexes from.(DVB-S)\n"
            "      -S <file>     : DVB-S/S2 Satellite  channels.conf file to import services and \n"
            "                      multiplexes from. NOTE: File must be in VDR format!\n"
            "      -l <LNB Type> : (DVB-S Only) Set LNB type being used\n"
            "                      (Use -l help to print types) or \n"
            "      -l <low>,<high>,<switch> Specify LO frequencies in MHz\n"
            "\n"
            "      -c <file>     : Cable channels.conf file to import services and \n"
            "                      multiplexes from. (DVB-C)\n"
            "\n"
#endif
            "      -i <file>     : ISDB channels.conf file to import services and \n"
            "                      multiplexes from. (ISDB-T)  (EXPERIMENTAL)\n"
            "                      NOTE: The file should be in dvb-t format\n"
            "\n"
#if defined(ENABLE_ATSC)
            "      -A <file>     : ATSC channels.conf file to import services and \n"
            "                      multiplexes from. (ATSC)\n"
#endif
            ,appname );
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

