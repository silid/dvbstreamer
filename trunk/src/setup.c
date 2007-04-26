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
#include "lnb.h"


#define INIT(_func, _name) \
    do {\
        if (_func) \
        { \
            printlog(LOG_ERROR, "Failed to initialise %s.\n", _name); \
            exit(1);\
        }\
        printlog(LOG_DEBUGV, "Initialised %s.\n", _name);\
    }while(0)

#define DEINIT(_func, _name) \
    do {\
        _func;\
        printlog(LOG_DEBUGV, "Deinitialised %s\n", _name);\
    }while(0)

static void usage(char *appname);
static void version(void);


char DataDirectory[PATH_MAX];

int main(int argc, char *argv[])
{
    fe_type_t channelsFileType = FE_OFDM;
    char *channelsFile = NULL;
    int adapterNumber = 0;
    LNBInfo_t lnbInfo = {NULL,NULL,0,0,0};
    
    /* Create the data directory */
    sprintf(DataDirectory, "%s/.dvbstreamer", getenv("HOME"));
    mkdir(DataDirectory, S_IRWXU);

    while (TRUE)
    {
        int c;
        c = getopt(argc, argv, "vVdro:a:t:s:c:l:h");
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
            case 'a':
                adapterNumber = atoi(optarg);
                printlog(LOG_INFOV, "Using adapter %d\n", adapterNumber);
                break;
                /* Database initialisation options*/
            case 't':
                channelsFile = optarg;
                channelsFileType = FE_OFDM;
                printlog(LOG_INFOV, "Using DVB-T channels file %s\n", channelsFile);
                break;
            case 's':
                channelsFile = optarg;
                channelsFileType = FE_QPSK;
                printlog(LOG_INFOV, "Using DVB-S channels file %s\n", channelsFile);
                break;
            case 'c':
                channelsFile = optarg;
                channelsFileType = FE_QAM;
                printlog(LOG_INFOV, "Using DVB-C channels file %s\n", channelsFile);
                break;
            case 'A':
                channelsFile = optarg;
                channelsFileType = FE_ATSC;
                printlog(LOG_INFOV, "Using ATSC channels file %s\n", channelsFile);
                break;
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
            case 'h':
            default:
                usage(argv[0]);
                exit(1);
        }
    }

    if ((channelsFileType == FE_QPSK) && (lnbInfo.lowFrequency == 0))
    {
        fprintf(stderr, "No LNB information provide for DVB-S channels.conf file!\n");
        exit(1);
    }
    
    INIT(ObjectInit(), "objects");
    INIT(MultiplexInit(), "multiplex");
    INIT(ServiceInit(), "service");
    INIT(DBaseInit(adapterNumber), "database");

    if (!channelsFile)
    {
        usage(argv[0]);
        exit(1);
    }

    sqlite3_exec(DBaseInstance, "BEGIN TRANSACTION;", NULL, NULL, NULL);
    
    printlog(LOG_INFO,"Importing services from %s\n", channelsFile);
    if (!parsezapfile(channelsFile, channelsFileType))
    {
        exit(1);
    }

    if (channelsFileType == FE_QPSK)
    {
        /* Write out LNB settings. */
        DBaseMetadataSetInt(METADATA_NAME_LNB_LOW_FREQ, lnbInfo.lowFrequency * 1000);
        DBaseMetadataSetInt(METADATA_NAME_LNB_HIGH_FREQ, lnbInfo.highFrequency * 1000);
        DBaseMetadataSetInt(METADATA_NAME_LNB_SWITCH_FREQ, lnbInfo.switchFrequency * 1000);        
    }
    
    sqlite3_exec(DBaseInstance, "COMMIT TRANSACTION;", NULL, NULL, NULL);    
    
    printf("%d Services available on %d Multiplexes\n", ServiceCount(), MultiplexCount());

    DEINIT(ServiceDeinit(), "service");
    DEINIT(MultiplexDeinit(), "multiplex");
    DEINIT(DBaseDeInit(), "database");
    DEINIT(ObjectDeinit(), "objects");

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
            "      -V            : Print version information then exit\n"
            "\n"
            "      -a <adapter>  : Use adapter number (ie /dev/dvb/adapter<adapter>/...)\n"
            "\n"
            "      -t <file>     : Terrestrial channels.conf file to import services and \n"
            "                      multiplexes from. (DVB-T)\n"
            "\n"
            "      -s <file>     : Satellite channels.conf file to import services and \n"
            "                      multiplexes from.(DVB-S)\n"
            "      -l <LNB Type> : (DVB-S Only) Set LNB type being used\n"
            "                      (Use -l help to print types) or \n"
            "      -l <low>,<high>,<switch> Specify LO frequencies in MHz\n"
            "\n"
            "      -c <file>     : Cable channels.conf file to import services and \n"
            "                      multiplexes from. (DVB-C)\n"
            "\n"
            "      -A <file>     : ATSC channels.conf file to import services and \n"
            "                      multiplexes from. (ATSC) (EXPERIMENTAL)\n",
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
