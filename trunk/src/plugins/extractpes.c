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

extractpes.c

Example use of the PES Filter Feature.

*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdint.h>

#include "plugin.h"
#include "ts.h"
#include "logging.h"
#include "pesprocessor.h"
#include "deliverymethod.h"

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void CommandStartExtractingPes(int argc, char **argv);
static void CommandStopExtractingPes(int argc, char **argv);
static void CommandCurrentExtractingPes(int argc, char **argv);
static void ProcessPESPacket(void *userarg, uint8_t *packet, uint16_t length);
/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static PIDFilter_t pesOutput;
static uint16_t pid;
static bool started = FALSE;

/*******************************************************************************
* Plugin Setup                                                                 *
*******************************************************************************/
PLUGIN_COMMANDS(
    {
        "startxpes",
        TRUE, 2, 2,
        "Start extracting a PES from a specified PID to an MRL.",
        "Start extracting a Packetised Elementary Stream on the specified PID"
        "and send it to the specified MRL.",
        CommandStartExtractingPes
    },
    {
        "stopxpes",
        FALSE, 0, 0,
        "Stop extracting a PES.",
        "Stop a previously started extraction of a PES from a PID.",
        CommandStopExtractingPes
    },
    {
        "currentxpes",
        FALSE, 0, 0,
        "Display the current PID being extracted.",
        "Displays the current PID from which a PES is being extracted.",
        CommandCurrentExtractingPes
    }
);

PLUGIN_INTERFACE_C(
    PLUGIN_FOR_ALL,
    "ExtractPES",
    "0.1",
    "Example usage of the PES Filter.",
    "charrea6@users.sourceforge.net"
);


/*******************************************************************************
* Command functions                                                            *
*******************************************************************************/
static void CommandStartExtractingPes(int argc, char **argv)
{

    if (started)
    {
        CommandError(COMMAND_ERROR_GENERIC, "Already extracting a PES!");
        return;
    }

    pid = (uint16_t)atoi(argv[0]);
    if (pid == 0)
    {
        CommandError(COMMAND_ERROR_GENERIC, "Unknown PID!");
        return;
    }

    memset(&pesOutput, 0, sizeof(pesOutput));

    if (!DeliveryMethodManagerFind(argv[1], &pesOutput))
    {
        CommandError(COMMAND_ERROR_GENERIC, "Failed to create output!");
        return;
    }
    pesOutput.packetsOutput = 0;
    PESProcessorStartPID(pid, ProcessPESPacket, NULL);
    started = TRUE;

}

static void CommandStopExtractingPes(int argc, char **argv)
{
    if (!started)
    {
        CommandError(COMMAND_ERROR_GENERIC, "Not extracting a PES!");
        return;
    }
    DeliveryMethodManagerFree(&pesOutput);
    PESProcessorStopPID(pid, ProcessPESPacket, NULL);
    started = FALSE;
}
static void CommandCurrentExtractingPes(int argc, char **argv)
{
    if (!started)
    {
        CommandError(COMMAND_ERROR_GENERIC, "Not extracting a PES!");
        return;
    }
    CommandPrintf("PID          : %d\n", pid);
    CommandPrintf("Packet Count : %d\n", pesOutput.packetsOutput);
}
/*******************************************************************************
* Packet processing                                                            *
*******************************************************************************/
static void ProcessPESPacket(void *userarg, uint8_t *packet, uint16_t length)
{
    DeliveryMethodInstance_t *dmInstance;
    if (!started)
    {
        return;
    }

    dmInstance = pesOutput.opArg;
    dmInstance->ops->SendBlock(dmInstance, packet, length);
    pesOutput.packetsOutput ++;
}

