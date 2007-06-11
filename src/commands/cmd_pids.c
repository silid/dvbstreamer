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

pids.c

Command functions for pid/manual output related tasks.

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


/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void CommandPids(int argc, char **argv);
static void CommandAddOutput(int argc, char **argv);
static void CommandRmOutput(int argc, char **argv);
static void CommandOutputs(int argc, char **argv);
static void CommandSetOutputMRL(int argc, char **argv);
static void CommandAddPID(int argc, char **argv);
static void CommandRmPID(int argc, char **argv);
static void CommandOutputPIDs(int argc, char **argv);

static int ParsePID(char *argument);
static char *Trim(char *str);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/

Command_t CommandDetailsPIDs[] = 
{
    {
      "pids",
      FALSE, 1, 1,
      "List the PIDs for a specified service.",
      "pids <service name>\n"
      "List the PIDs for <service name>.",
      CommandPids
    },
    {
        "addoutput",
        TRUE, 2, 2,
        "Add a new destination for manually filtered PIDs.",
        "addoutput <output name> <mrl>\n"
        "Adds a new destination for sending packets to. This is only used for "
        "manually filtered packets. "
        "To send packets to this destination you'll need to also call \'addpid\' "
        "with this output as an argument.",
        CommandAddOutput
    },
    {
        "rmoutput",
        TRUE, 1, 1,
        "Remove a destination for manually filtered PIDs.",
        "rmoutput <output name>\n"
        "Removes the destination and stops all filters associated with this output.",
        CommandRmOutput
    },
    {
        "lsoutputs",
        FALSE, 0, 0,
        "List current outputs.",
        "List all active additonal output names and destinations.",
        CommandOutputs
    },
    {
        "setoutputmrl",
        TRUE, 2, 2,
        "Set the output's MRL.",
        "setoutputmrl <output name> <mrl>\n"
        "Change the destination for packets sent to this output. If the MRL cannot be"
        " parsed no change will be made to the output.",
        CommandSetOutputMRL
    },
    {
        "addpid",
        TRUE, 2, 2,
        "Adds a PID to filter to an output.",
        "addpid <output name> <pid>\n"
        "Adds a PID to the filter to be sent to the specified output. The PID can be "
        "specified in either hex (starting with 0x) or decimal format.",
        CommandAddPID
    },
    {
        "rmpid",
        TRUE, 2, 2,
        "Removes a PID to filter from an output.",
        "rmpid <output name> <pid>\n"
        "Removes the PID from the filter that is sending packets to the specified output."
        "The PID can be specified in either hex (starting with 0x) or decimal format.",
        CommandRmPID
    },
    {
        "lspids",
        TRUE, 1, 1,
        "List PIDs for output.",
        "lspids <output name>\n"
        "List the PIDs being filtered for a specific output.",
        CommandOutputPIDs
    },
    {NULL, FALSE, 0, 0, NULL,NULL}
};

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
void CommandInstallPids(void)
{
    CommandRegisterCommands(CommandDetailsPIDs);
}

void CommandUnInstallPids(void)
{
    CommandUnRegisterCommands(CommandDetailsPIDs);    
}

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/

static void CommandPids(int argc, char **argv)
{
    Service_t *service;

    service = ServiceFindName(argv[0]);
    if (service)
    {
        bool cached = TRUE;
        int i;
        PIDList_t *pids;
        pids = CachePIDsGet(service);
        if (pids == NULL)
        {
            pids = PIDListGet(service);
            cached = FALSE;
        }

        if (pids)
        {
            CommandPrintf("%d PIDs for \"%s\" %s\n", pids->count, argv[0], cached ? "(Cached)":"");
            for (i = 0; i < pids->count; i ++)
            {
                CommandPrintf("%2d: %d %d %d\n", i, pids->pids[i].pid, pids->pids[i].type, pids->pids[i].subType);
            }

            if (cached)
            {
                CachePIDsRelease();
            }
            else
            {
                PIDListFree(pids);
            }
        }
        else
        {
            CommandPrintf("0 PIDs for \"%s\"\n",argv[0]);
        }
        ServiceRefDec(service);
    }
    else
    {
        CommandError(COMMAND_ERROR_GENERIC, "Service not found!");
    }
}

static void CommandAddOutput(int argc, char **argv)
{
    Output_t *output = NULL;
    DVBAdapter_t *adapter = MainDVBAdapterGet();
    if (adapter->hardwareRestricted)
    {
        CommandError(COMMAND_ERROR_GENERIC, "Not supported in hardware restricted mode!");
        return;
    }
    CommandCheckAuthenticated();

    output = OutputAllocate(argv[0], OutputType_Manual, argv[1]);
    if (!output)
    {
        CommandError(COMMAND_ERROR_GENERIC, OutputErrorStr);
    }
}

static void CommandRmOutput(int argc, char **argv)
{
    Output_t *output = NULL;

    CommandCheckAuthenticated();

    if (strcmp(argv[0], PrimaryService) == 0)
    {
        CommandError(COMMAND_ERROR_GENERIC,"Cannot remove the primary output!");
        return;
    }

    output = OutputFind(argv[0], OutputType_Manual);
    if (output == NULL)
    {
        return;
    }
    OutputFree(output);
}

static void CommandOutputs(int argc, char **argv)
{
    ListIterator_t iterator;
    for ( ListIterator_Init(iterator, ManualOutputsList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        Output_t *output = ListIterator_Current(iterator);
        CommandPrintf("%10s : %s\n",output->name,
                DeliveryMethodGetMRL(output->filter));
    }
}

static void CommandSetOutputMRL(int argc, char **argv)
{
    Output_t *output = NULL;

    CommandCheckAuthenticated();

    output = OutputFind(argv[0], OutputType_Manual);
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
        CommandError(COMMAND_ERROR_GENERIC,"Failed to set MRL");
    }
}
static void CommandAddPID(int argc, char **argv)
{
    Output_t *output;
    CommandCheckAuthenticated();

    output = OutputFind(argv[0], OutputType_Manual);
    if (output)
    {
        int i;
        i = ParsePID(argv[1]);
        if (i < 0)
        {
            return;
        }
        OutputAddPID(output, (uint16_t)i);
    }
}

static void CommandRmPID(int argc, char **argv)
{
    Output_t *output;

    CommandCheckAuthenticated();

    output = OutputFind(argv[0], OutputType_Manual);
    if (output)
    {
        int i;
        i = ParsePID(argv[1]);
        if (i < 0)
        {
            return;
        }
        OutputRemovePID(output, (uint16_t)i);
    }
}

static void CommandOutputPIDs(int argc, char **argv)
{
    int i;
    Output_t *output = NULL;
    int pidcount;
    uint16_t *pids;
    char *name;

    name = Trim(argv[0]);

    output = OutputFind(name, OutputType_Manual);

    if (!output)
    {
        return;
    }
    OutputGetPIDs(output, &pidcount, &pids);

    CommandPrintf("PIDs for \'%s\' (%d):\n", name, pidcount);

    for (i = 0; i <pidcount; i ++)
    {
        CommandPrintf("0x%x\n", pids[i]);
    }

}

/*******************************************************************************
* Helper Functions                                                             *
*******************************************************************************/
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
        CommandPrintf("Failed to parse \"%s\"\n", argument);
        return -1;
    }

    return pid;
}

static char *Trim(char *str)
{
    char *result;
    char *end;
    /* Trim spaces from the start of the address */
    for(result = str; *result && isspace(*result); result ++);

    /* Trim spaces from the end of the address */
    for (end = result + (strlen(result) - 1); (result != end) && isspace(*end); end --)
    {
        *end = 0;
    }
    return result;
}
