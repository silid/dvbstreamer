/*
Copyright (C) 2010  Adam Charrett

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

pipeoutput.c

Pipe/Named Fifo Delivery Method handler.

*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "plugin.h"
#include "ts.h"
#include "deliverymethod.h"
#include "logging.h"

/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/
struct PipeOutputInstance_t
{
    /* !!! MUST BE THE FIRST FIELD IN THE STRUCTURE !!!
     * As the address of this field will be passed to all delivery method
     * functions and a 0 offset is assumed!
     */
    DeliveryMethodInstance_t instance;

    int fd;
};


/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
bool PipeOutputCanHandle(char *mrl);
DeliveryMethodInstance_t *PipeOutputCreate(char *arg);
void PipeOutputSendPacket(DeliveryMethodInstance_t *this, TSPacket_t *packet);
void PipeOutputSendBlock(DeliveryMethodInstance_t *this, void *block, unsigned long blockLen);
void PipeOutputDestroy(DeliveryMethodInstance_t *this);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
/** Constants for the start of the MRL **/
const char PipePrefix[] = "pipe://";

DeliveryMethodInstanceOps_t PipeInstanceOps ={
    PipeOutputSendPacket,
    PipeOutputSendBlock,
    PipeOutputDestroy,
    NULL,
    NULL,
};

static const char PIPEOUTPUT[] = "PipeOutput";

/*******************************************************************************
* Plugin Setup                                                                 *
*******************************************************************************/
PLUGIN_FEATURES(
    PLUGIN_FEATURE_DELIVERYMETHOD(PipeOutputCanHandle, PipeOutputCreate)
);

PLUGIN_INTERFACE_F(
    PLUGIN_FOR_ALL,
    "PipeOutput",
    "0.1",
    "Pipe/Named fifo Delivery method.\nUse pipe://<file name>\n"
    "File name can be in absolute or relative.\n"
    "For an absolute file name use pipe:///home/user/mypipe.\n"
    "For a relative file name use file://mypipe.\n",
    "charrea6@users.sourceforge.net"
);

/*******************************************************************************
* Delivery Method Functions                                                    *
*******************************************************************************/

bool PipeOutputCanHandle(char *mrl)
{
    return (strncmp(PipePrefix, mrl, sizeof(PipePrefix)-1) == 0);
}

DeliveryMethodInstance_t *PipeOutputCreate(char *arg)
{
    struct PipeOutputInstance_t *instance = calloc(1, sizeof(struct PipeOutputInstance_t));
    char *path = (char*)(arg + (sizeof(PipePrefix)-1));
    struct stat statInfo;
    if (instance == NULL)
    {
        return NULL;
    }
    instance->instance.ops = &PipeInstanceOps;
    if (stat(path, &statInfo) == -1)
    {
        /* path doesn't exist try and create it */
        if (mkfifo(path, 0666) == -1)
        {
            free(instance);
            return NULL;
        }
    }
    else
    {
        if (!S_ISFIFO(statInfo.st_mode))
        {
            free(instance);
            return NULL;
        }
    }
        
    instance->fd = open(path, O_RDWR);

    if (instance->fd == -1)
    {
        free(instance);
        return NULL;
    }

    instance->instance.mrl = strdup(arg);
    return &instance->instance;
}

void PipeOutputSendPacket(DeliveryMethodInstance_t *this, TSPacket_t *packet)
{
    PipeOutputSendBlock(this, (void *)packet, sizeof(TSPacket_t));
}

void PipeOutputSendBlock(DeliveryMethodInstance_t *this, void *block, unsigned long blockLen)
{
    struct PipeOutputInstance_t *instance = (struct PipeOutputInstance_t*)this;
    if (write(instance->fd, block, blockLen) != blockLen)
    {
        LogModule(LOG_INFO, PIPEOUTPUT, "Failed to write entire block to pipe!\n");
    }
}

void PipeOutputDestroy(DeliveryMethodInstance_t *this)
{
    struct PipeOutputInstance_t *instance = (struct PipeOutputInstance_t*)this;
    close(instance->fd);
    free(this->mrl);
    free(this);
}

