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

servicefilter.c

Command functions for service filter related tasks

*/
#define _GNU_SOURCE
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include <getopt.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "commands.h"
#include "multiplexes.h"
#include "services.h"
#include "dvb.h"
#include "ts.h"
#include "logging.h"
#include "cache.h"
#include "outputs.h"
#include "main.h"
#include "deliverymethod.h"
#include "plugin.h"
#include "servicefilter.h"
#include "tuning.h"
#include "patprocessor.h"
#include "pmtprocessor.h"
#include "sdtprocessor.h"
#include "psipprocessor.h"

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/

static void CommandAddSSF(int argc, char **argv);
static void CommandRemoveSSF(int argc, char **argv);
static void CommandSSFS(int argc, char **argv);
static void CommandSetSFAVSOnly(int argc, char **argv);
static void CommandSetSSF(int argc, char **argv);
static void CommandSetSFMRL(int argc, char **argv);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/

Command_t CommandDetailsServiceFilter[] = 
{
    {
        "addsf",
        TRUE, 2, 2,
        "Add a service filter for secondary services.",
        "addsf <output name> <mrl>\n"
        "Adds a new destination for sending a secondary service to.",
        CommandAddSSF
    },
    {
        "rmsf",
        TRUE, 1, 1,
        "Remove a service filter for secondary services.",
        "rmsf <output name>\n"
        "Remove a destination for sending secondary services to.",
        CommandRemoveSSF
    },
    {
        "lssfs",
        FALSE,0,0,
        "List all secondary service filters.",
        "List all secondary service filters their names, destinations and currently selected service.",
        CommandSSFS
    },
    {
        "setsf",
        FALSE, 1, 1,
        "Select a service to stream to a secondary service output.",
        "setsf <output name> <service name>\n"
        "Stream the specified service to the secondary service output.",
        CommandSetSSF
    },
    {
        "setsfmrl",
        TRUE, 2, 2,
        "Set the service filter's MRL.",
        "setsfmrl <output name> <mrl>\n"
        "Change the destination for packets sent to this service filters output."
        "If the MRL cannot be parsed no change will be made to the service filter.",
        CommandSetSFMRL
    },
    {
        "setsfavsonly",
        TRUE, 2, 2,
        "Enable/disable streaming of Audio/Video/Subtitles only.",
        "setsfavsonly <output name> on|off\n"
        "Enabling AVS Only cause the PMT to be rewritten to only include the first "
        "video stream, normal audio stream and the subtitles stream only.",
        CommandSetSFAVSOnly
    },
    {NULL, FALSE, 0, 0, NULL,NULL}
};
/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
void CommandInstallServiceFilter(void)
{
    CommandRegisterCommands(CommandDetailsServiceFilter);
}

void CommandUnInstallServiceFilter(void)
{
    CommandUnRegisterCommands(CommandDetailsServiceFilter);    
}

/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/

static void CommandAddSSF(int argc, char **argv)
{
    Output_t *output = NULL;

    CommandCheckAuthenticated();

    output = OutputAllocate(argv[0], OutputType_Service, argv[1]);
    if (!output)
    {
        CommandError(COMMAND_ERROR_GENERIC, OutputErrorStr);
    }
}

static void CommandRemoveSSF(int argc, char **argv)
{
    Output_t *output = NULL;
    Service_t *oldService;

    CommandCheckAuthenticated();

    if (strcmp(argv[0], PrimaryService) == 0)
    {
        CommandError(COMMAND_ERROR_GENERIC,"You cannot remove the primary service!");
        return;
    }

    output = OutputFind(argv[0], OutputType_Service);
    if (output == NULL)
    {
        return;
    }
    OutputGetService(output, &oldService);
    OutputFree(output);
    if (oldService)
    {
        ServiceRefDec(oldService);
    }

}

static void CommandSSFS(int argc, char **argv)
{
    ListIterator_t iterator;
    for ( ListIterator_Init(iterator, ServiceOutputsList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        Output_t *output = ListIterator_Current(iterator);
        Service_t *service;
        OutputGetService(output, &service);
        CommandPrintf("%10s : %s (%s)\n",output->name,
                DeliveryMethodGetMRL(output->filter),
                service ? service->name:"<NONE>");
    }
}

static void CommandSetSSF(int argc, char **argv)
{
    Output_t *output = NULL;
    char *outputName = argv[0];
    char *serviceName;
    Service_t *service;

    CommandCheckAuthenticated();

    serviceName = strchr(outputName, ' ');
    if (!serviceName)
    {
        CommandError(COMMAND_ERROR_GENERIC,"No service specified!");
        return;
    }

    serviceName[0] = 0;

    for (serviceName ++;*serviceName && isspace(*serviceName); serviceName ++);

    if (strcmp(outputName, PrimaryService) == 0)
    {
        CommandError(COMMAND_ERROR_GENERIC,"Use \'select\' to change the primary service!");
        return;
    }

    output = OutputFind(outputName, OutputType_Service);
    if (output == NULL)
    {
        CommandError(COMMAND_ERROR_GENERIC,"Failed to find output!");
        return;
    }

    service = ServiceFindName(serviceName);
    if (service == NULL)
    {
        CommandPrintf("Failed to find service %s\n", serviceName);
        return;
    }

    if (OutputSetService(output, service))
    {
        ServiceRefDec(service);
        CommandError(COMMAND_ERROR_GENERIC,"Failed to find multiplex for service");
        return;
    }

    ServiceRefDec(service);
}

static void CommandSetSFMRL(int argc, char **argv)
{
    Output_t *output = NULL;

    CommandCheckAuthenticated();

    output = OutputFind(argv[0], OutputType_Service);
    if (output == NULL)
    {
        return;
    }
    if (DeliveryMethodManagerFind(argv[1], output->filter))
    {
        CommandPrintf("MRL set to \"%s\" for %s\n", DeliveryMethodGetMRL(output->filter), argv[0]);
    }
    else
    {
        CommandError(COMMAND_ERROR_GENERIC,"Failed to set MRL!");
    }
}

static void CommandSetSFAVSOnly(int argc, char **argv)
{
    Output_t *output = NULL;

    CommandCheckAuthenticated();

    output = OutputFind(argv[0], OutputType_Service);
    if (output == NULL)
    {
        return;
    }
    if (strcasecmp(argv[1], "on") == 0)
    {
        ServiceFilterAVSOnlySet(output->filter, TRUE);
    }
    else if (strcasecmp(argv[1], "off") == 0)
    {
        ServiceFilterAVSOnlySet(output->filter, FALSE);
    }
    else
    {
        CommandError(COMMAND_ERROR_WRONG_ARGS,"Need to specify on or off.\n");
    }
}


