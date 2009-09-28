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
#include "dvbadapter.h"
#include "ts.h"
#include "logging.h"
#include "cache.h"
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
* Defines                                                                      *
*******************************************************************************/

#define FIND_SERVICE_FILTER(_name) \
    filter = ServiceFilterFindFilter(_name); \
    if (filter == NULL) \
    {\
        CommandError(COMMAND_ERROR_GENERIC, "Service filter not found!"); \
        return; \
    }

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void CommandSelect(int argc, char **argv);
static void CommandSetMRL(int argc, char **argv);
static void CommandGetMRL(int argc, char **argv);

static void CommandAddSF(int argc, char **argv);
static void CommandRemoveSF(int argc, char **argv);
static void CommandListSF(int argc, char **argv);

static void CommandSetSFAVSOnly(int argc, char **argv);
static void CommandGetSFAVSOnly(int argc, char **argv);

static void CommandSetSFService(int argc, char **argv);
static void CommandGetSFService(int argc, char **argv);

static void CommandSetSFMRL(int argc, char **argv);
static void CommandGetSFMRL(int argc, char **argv);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/

Command_t CommandDetailsServiceFilter[] = 
{
    {
        "select",
        1, 1,
        "Select a new service to stream.",
        "select <service name>\n"
        "Sets <service name> as the current service, this may mean tuning to a different "
        "multiplex.",
        CommandSelect
    },
    {
        "setmrl",
        1,1,
        "Set the MRL of the primary service filter.",
        "setmrl <MRL>\n"
        "Set the MRL of the primary service filter.\n"
        "NOTE: This is actually an alias of setsfmrl called with <Primary> as the service filter name.",
        CommandSetMRL
    },
    {
        "getmrl",
        0,0,
        "Get the primary service filter MRL.",
        "getmrl\n"
        "Get the MRL of the primary service filter.\n"
        "NOTE: This is actually an alias of getsfmrl called with <Primary> as the service filter name.",
        CommandGetMRL
    },    
    {
        "addsf",
        2, 2,
        "Add a service filter.",
        "addsf <service filter name> <mrl>\n"
        "Adds a new destination for sending a secondary service to.",
        CommandAddSF
    },
    {
        "rmsf",
        1, 1,
        "Remove a service filter.",
        "rmsf <service filter name>\n"
        "Remove a destination for sending secondary services to.",
        CommandRemoveSF
    },
    {
        "lssfs",
        0, 0,
        "List all service filters.",
        "List all service filters their names, destinations and currently selected service.",
        CommandListSF
    },
    {
        "setsf",
        2, 2,
        "Set the service to be filtered by a service filter.",
        "setsf <service filter name> <service name>\n"
        "Selects the service to be filtered by the service filter.\n"
        "Cannot be used for the primary service filter (<Primary>), use \'select\' instead",
        CommandSetSFService
    },
{
        "getsf",
        1, 1,
        "Get the service to stream to a secondary service output.",
        "setsf <service filter name> <service name>\n"
        "Stream the specified service to the secondary service output.",
        CommandGetSFService
    },    
    {
        "setsfmrl",
        2, 2,
        "Set the service filter's MRL.",
        "setsfmrl <service filter name> <mrl>\n"
        "Change the destination for packets sent to this service filters output."
        "If the MRL cannot be parsed no change will be made to the service filter.",
        CommandSetSFMRL
    },
    {
        "getsfmrl",
        1, 1,
        "Get the service filter's MRL.",
        "getsfmrl <service filter name>\n"
        "Retrieve the current MRL for the specified service filter.",
        CommandGetSFMRL
    },    
    {
        "setsfavsonly",
        2, 2,
        "Enable/disable streaming of Audio/Video/Subtitles only.",
        "setsfavsonly <service filter name> on|off\n"
        "Enabling AVS Only cause the PMT to be rewritten to only include the first "
        "video stream, normal audio stream and the subtitles stream only.\n"
        "(Default: off)",
        CommandSetSFAVSOnly
    },
    {
        "getsfavsonly",
        1, 1,
        "Get whether Audio/Video/Subtitles only streaming is enabled.",
        "getsfavsonly <service filter name>\n"
        "Retrieves whether Audio/Video/Subtitles only streaming is enabled.",
        CommandGetSFAVSOnly
    },
    COMMANDS_SENTINEL
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

static void CommandSelect(int argc, char **argv)
{
    Service_t *service;

    CommandCheckAuthenticated();

    UpdateDatabase();
    service = ServiceFind(argv[0]);
    
    if (service)
    {
        Multiplex_t *multiplex;
        TuningCurrentServiceSet(service);
        
        multiplex = TuningCurrentMultiplexGet();
        CommandPrintf("%04x.%04x.%04x : \"%s\"\n", 
            multiplex->networkId, multiplex->tsId, service->id, service->name);
        ServiceRefDec(service);
        MultiplexRefDec(multiplex);
    }
    else
    {
        CommandError(COMMAND_ERROR_GENERIC, "Service not found!");
    }
}

static void CommandSetMRL(int argc, char **argv)
{
    char *tmpArgs[2];
    tmpArgs[0] = (char*)PrimaryService;
    tmpArgs[1] = argv[0];
    CommandSetSFMRL(2, tmpArgs);
}

static void CommandGetMRL(int argc, char **argv)
{
    char *tmpArgs[1];
    tmpArgs[0] = (char*)PrimaryService;
    CommandGetSFMRL(1, tmpArgs);
}


static void CommandAddSF(int argc, char **argv)
{
    TSReader_t *tsReader = MainTSReaderGet();
    ServiceFilter_t filter;
    
    CommandCheckAuthenticated();
    filter = ServiceFilterFindFilter(argv[0]);
    if (filter)
    {
        CommandError(COMMAND_ERROR_GENERIC, "Service Filter of that name already exists!");
    }
    else
    {
        DeliveryMethodInstance_t *instance;
        filter = ServiceFilterCreate(tsReader, strdup(argv[0]));
        instance = DeliveryMethodCreate(argv[1]);
        if (!instance)
        {
            instance = DeliveryMethodCreate("null://");
        }
        ServiceFilterDeliveryMethodSet(filter, instance);
    }
}

static void CommandRemoveSF(int argc, char **argv)
{
    ServiceFilter_t filter;

    CommandCheckAuthenticated();

    if (strcmp(argv[0], PrimaryService) == 0)
    {
        CommandError(COMMAND_ERROR_GENERIC,"You cannot remove the primary service!");
        return;
    }

    FIND_SERVICE_FILTER(argv[0]);
    
    ServiceFilterDestroy(filter);
}

static void CommandListSF(int argc, char **argv)
{
    ListIterator_t *iterator;
    
    for (iterator = ServiceFilterGetListIterator(); ListIterator_MoreEntries(*iterator); ListIterator_Next(*iterator))
    {
        ServiceFilter_t filter = ListIterator_Current(*iterator);
        Service_t *service = ServiceFilterServiceGet(filter);
        char *name = ServiceFilterNameGet(filter);
        DeliveryMethodInstance_t *dmInstance = ServiceFilterDeliveryMethodGet(filter);
        CommandPrintf("%10s : %s (%s)\n", name,
                DeliveryMethodGetMRL(dmInstance),
                service ? service->name:"<NONE>");
    }
}

static void CommandSetSFService(int argc, char **argv)
{
    ServiceFilter_t filter;
    char *outputName = argv[0];
    char *serviceName = argv[1];
    Service_t *service;

    CommandCheckAuthenticated();

    if (strcmp(outputName, PrimaryService) == 0)
    {
        CommandError(COMMAND_ERROR_GENERIC,"Use \'select\' to change the primary service!");
        return;
    }

    FIND_SERVICE_FILTER(outputName);

    service = ServiceFindName(serviceName);
    if (service == NULL)
    {
        /* Attempt to look up the service using the fully qualified name */
        service = ServiceFindFQIDStr(serviceName);
    }
    
    if (service == NULL)
    {
        CommandError(COMMAND_ERROR_GENERIC,"Service not found!");
        return;
    }

    ServiceFilterServiceSet(filter,service);

    ServiceRefDec(service);
}

static void CommandGetSFService(int argc, char **argv)
{
    ServiceFilter_t filter;
    Service_t *service;
    Multiplex_t *multiplex;
 
    FIND_SERVICE_FILTER(argv[0]);
    service = ServiceFilterServiceGet(filter);
    multiplex = MultiplexFindUID(service->multiplexUID);
    
    CommandPrintf("%04x.%04x.%04x : \"%s\"\n",
        multiplex->networkId & 0xffff, multiplex->tsId & 0xffff, service->id & 0xffff,
        service->name);
    
    MultiplexRefDec(multiplex);
}

static void CommandSetSFMRL(int argc, char **argv)
{
    ServiceFilter_t filter;
    DeliveryMethodInstance_t *instance;
    CommandCheckAuthenticated();

    FIND_SERVICE_FILTER(argv[0]);

    instance = DeliveryMethodCreate(argv[1]);
    if (instance)
    {
        ServiceFilterDeliveryMethodSet(filter, instance);
        CommandPrintf("MRL set to \"%s\" for %s\n", DeliveryMethodGetMRL(ServiceFilterDeliveryMethodGet(filter)), argv[0]);
    }
    else
    {
        CommandError(COMMAND_ERROR_GENERIC,"Failed to set MRL!");
    }
}

static void CommandGetSFMRL(int argc, char **argv)
{
    ServiceFilter_t filter;
    CommandCheckAuthenticated();

    FIND_SERVICE_FILTER(argv[0]);

    CommandPrintf("%s\n", DeliveryMethodGetMRL(ServiceFilterDeliveryMethodGet(filter)));
}

static void CommandSetSFAVSOnly(int argc, char **argv)
{
    ServiceFilter_t filter;
    CommandCheckAuthenticated();

    FIND_SERVICE_FILTER(argv[0]);

    if (strcasecmp(argv[1], "on") == 0)
    {
        ServiceFilterAVSOnlySet(filter, TRUE);
    }
    else if (strcasecmp(argv[1], "off") == 0)
    {
        ServiceFilterAVSOnlySet(filter, FALSE);
    }
    else
    {
        CommandError(COMMAND_ERROR_WRONG_ARGS, "Need to specify on or off.\n");
    }
}

static void CommandGetSFAVSOnly(int argc, char **argv)
{
    ServiceFilter_t filter;
    bool avsOnly;
    CommandCheckAuthenticated();

    FIND_SERVICE_FILTER(argv[0]);

    avsOnly = ServiceFilterAVSOnlyGet(filter);

    CommandPrintf("%s : A/V/S Only = %s", avsOnly ? "On":"Off");
}

