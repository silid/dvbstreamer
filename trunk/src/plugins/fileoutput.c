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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "plugin.h"
#include "ts.h"
#include "deliverymethod.h"

struct FileOutputInstance_t
{
    char *mrl;
    void (*SendPacket)(DeliveryMethodInstance_t *this, TSPacket_t *packet);
    void (*DestroyInstance)(DeliveryMethodInstance_t *this);

    FILE *fp;
};

bool FileOutputCanHandle(char *mrl);
DeliveryMethodInstance_t *FileOutputCreate(char *arg);
void FileOutputSendPacket(DeliveryMethodInstance_t *this, TSPacket_t *packet);
void FileOutputDestroy(DeliveryMethodInstance_t *this);

/** Plugin Interface **/
DeliveryMethodHandler_t FileOutputHandler = {
    FileOutputCanHandle,
    FileOutputCreate
};

PLUGIN_FEATURES(
    PLUGIN_FEATURE_DELIVERYMETHOD(FileOutputHandler)
);

PLUGIN_INTERFACE_F("FileOutput", "0.1", "File Delivery method.\nUse file://<file name>\nFile name can be in absolute or relative.\nFor an absolute file name use file:///home/user/myts.ts.\nFor a relative file name use file://myts.ts.", "charrea6@users.sourceforge.net");

/** Constants for the start of the MRL **/
#define PREFIX_LEN (sizeof(FilePrefix) - 1)
char FilePrefix[] = "file://";

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
        instance->DestroyInstance = FileOutputDestroy;
        instance->fp = fopen(arg + PREFIX_LEN, "wb");
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
    struct FileOutputInstance_t *instance = (struct FileOutputInstance_t*)this;
    fwrite(packet, sizeof(TSPacket_t), 1, instance->fp);
}

void FileOutputDestroy(DeliveryMethodInstance_t *this)
{
    struct FileOutputInstance_t *instance = (struct FileOutputInstance_t*)this;
    fclose(instance->fp);
    free(this);
}
