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

fileoutput.c

File Delivery Method handler, all packets are written to the file of choosing.

*/
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "plugin.h"
#include "ts.h"
#include "deliverymethod.h"

/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/
struct FileOutputInstance_t
{
    char *mrl;
    void(*SendPacket)(DeliveryMethodInstance_t *this, TSPacket_t *packet);
    void(*SendBlock)(DeliveryMethodInstance_t *this, void *block, unsigned long blockLen);
    void(*DestroyInstance)(DeliveryMethodInstance_t *this);

    FILE *fp;
};


/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
bool FileOutputCanHandle(char *mrl);
DeliveryMethodInstance_t *FileOutputCreate(char *arg);
void FileOutputSendPacket(DeliveryMethodInstance_t *this, TSPacket_t *packet);
void FileOutputSendBlock(DeliveryMethodInstance_t *this, void *block, unsigned long blockLen);
void FileOutputDestroy(DeliveryMethodInstance_t *this);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
/** Constants for the start of the MRL **/
#define PREFIX_LEN (sizeof(FilePrefix) - 1)
const char FilePrefix[] = "file://";

/** Plugin Interface **/
DeliveryMethodHandler_t FileOutputHandler = {
            FileOutputCanHandle,
            FileOutputCreate
        };


/*******************************************************************************
* Plugin Setup                                                                 *
*******************************************************************************/
PLUGIN_FEATURES(
    PLUGIN_FEATURE_DELIVERYMETHOD(FileOutputHandler)
);

PLUGIN_INTERFACE_F(
    "FileOutput", 
    "0.1", 
    "File Delivery method.\nUse file://<file name>\n"
    "File name can be in absolute or relative.\n"
    "For an absolute file name use file:///home/user/myts.ts.\n"
    "For a relative file name use file://myts.ts.", 
    "charrea6@users.sourceforge.net"
);

/*******************************************************************************
* Delivery Method Functions                                                    *
*******************************************************************************/

bool FileOutputCanHandle(char *mrl)
{
    return (strncmp(FilePrefix, mrl, PREFIX_LEN) == 0);
}

DeliveryMethodInstance_t *FileOutputCreate(char *arg)
{
    struct FileOutputInstance_t *instance = calloc(1, sizeof(struct FileOutputInstance_t));

    if (instance)
    {
        instance->SendPacket = FileOutputSendPacket;
        instance->SendBlock = FileOutputSendBlock;
        instance->DestroyInstance = FileOutputDestroy;

        instance->fp = fopen((char*)(arg + PREFIX_LEN), "wb");

        if (!instance->fp)
        {
            free(instance);
            return NULL;
        }
    }
    return (DeliveryMethodInstance_t*)instance;
}

void FileOutputSendPacket(DeliveryMethodInstance_t *this, TSPacket_t *packet)
{
    FileOutputSendBlock(this, (void *)packet, sizeof(TSPacket_t));
}

void FileOutputSendBlock(DeliveryMethodInstance_t *this, void *block, unsigned long blockLen)
{
    struct FileOutputInstance_t *instance = (struct FileOutputInstance_t*)this;
    fwrite(block, blockLen, 1, instance->fp);
}

void FileOutputDestroy(DeliveryMethodInstance_t *this)
{
    struct FileOutputInstance_t *instance = (struct FileOutputInstance_t*)this;
    fclose(instance->fp);
    free(this);
}
