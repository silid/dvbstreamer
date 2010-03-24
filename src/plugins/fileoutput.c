/*
Copyright (C) 2006, 2010  Adam Charrett

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
#include "logging.h"

/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/
struct FileOutputInstance_t
{
    /* !!! MUST BE THE FIRST FIELD IN THE STRUCTURE !!!
     * As the address of this field will be passed to all delivery method
     * functions and a 0 offset is assumed!
     */
    DeliveryMethodInstance_t instance;

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
void FileReserveHeaderSpace(DeliveryMethodInstance_t *this, int packets);
void FileSetHeader(struct DeliveryMethodInstance_t *this,
                        TSPacket_t *packets, int count);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
/** Constants for the start of the MRL **/
const char FilePrefix[] = "file://";
const char FileAppendPrefix[] = "filea://";

DeliveryMethodInstanceOps_t FileInstanceOps ={
    FileOutputSendPacket,
    FileOutputSendBlock,
    FileOutputDestroy,
    FileReserveHeaderSpace,
    FileSetHeader,
};

static const char FILEOUTPUT[] = "FileOutput";

/*******************************************************************************
* Plugin Setup                                                                 *
*******************************************************************************/
PLUGIN_FEATURES(
    PLUGIN_FEATURE_DELIVERYMETHOD(FileOutputCanHandle, FileOutputCreate)
);

PLUGIN_INTERFACE_F(
    PLUGIN_FOR_ALL,
    "FileOutput",
    "0.2",
    "File Delivery method.\nUse file://<file name>\n"
    "File name can be in absolute or relative.\n"
    "For an absolute file name use file:///home/user/myts.ts.\n"
    "For a relative file name use file://myts.ts.\n"
    "Use the filea:// prefix to append data to an existing file.",
    "charrea6@users.sourceforge.net"
);

/*******************************************************************************
* Delivery Method Functions                                                    *
*******************************************************************************/

bool FileOutputCanHandle(char *mrl)
{
    return (strncmp(FilePrefix, mrl, sizeof(FilePrefix)-1) == 0) || 
           (strncmp(FileAppendPrefix, mrl, sizeof(FileAppendPrefix)-1) == 0);
}

DeliveryMethodInstance_t *FileOutputCreate(char *arg)
{
    struct FileOutputInstance_t *instance = calloc(1, sizeof(struct FileOutputInstance_t));
    bool append = (strncmp(FileAppendPrefix, arg, sizeof(FileAppendPrefix)-1) == 0);
    char *mode;
    int prefixLen;
    
    if (instance == NULL)
    {
        return NULL;
    }
    if (append)
    {
        mode = "ab";
        prefixLen = sizeof(FileAppendPrefix)-1;
    }
    else
    {
        mode = "wb";
        prefixLen = sizeof(FilePrefix)-1;
    }
    instance->instance.ops = &FileInstanceOps;

    instance->fp = fopen((char*)(arg + prefixLen), mode);

    if (!instance->fp)
    {
        free(instance);
        return NULL;
    }

    instance->instance.mrl = strdup(arg);
    return &instance->instance;
}

void FileOutputSendPacket(DeliveryMethodInstance_t *this, TSPacket_t *packet)
{
    FileOutputSendBlock(this, (void *)packet, sizeof(TSPacket_t));
}

void FileOutputSendBlock(DeliveryMethodInstance_t *this, void *block, unsigned long blockLen)
{
    struct FileOutputInstance_t *instance = (struct FileOutputInstance_t*)this;
    if (fwrite(block, 1, blockLen, instance->fp) != blockLen)
    {
        LogModule(LOG_INFO, FILEOUTPUT, "Failed to write entire block to file!\n");
    }
    fflush(instance->fp);
}

void FileOutputDestroy(DeliveryMethodInstance_t *this)
{
    struct FileOutputInstance_t *instance = (struct FileOutputInstance_t*)this;
    fclose(instance->fp);
    free(this->mrl);
    free(this);
}

void FileReserveHeaderSpace(DeliveryMethodInstance_t *this, int packets)
{
    struct FileOutputInstance_t *instance = (struct FileOutputInstance_t*)this;
    TSPacket_t nullPacket;
    int i;
    nullPacket.header[0] = 0x47;
    nullPacket.header[1] = 0x00;
    nullPacket.header[2] = 0x00;
    nullPacket.header[3] = 0x00;
    TSPACKET_SETPID(nullPacket, 0x1fff);

    for (i=0; i< packets; i ++)
    {
        if (fwrite(&nullPacket, TSPACKET_SIZE, 1, instance->fp) != 1)
        {
            LogModule(LOG_INFO, FILEOUTPUT, "Failed to write all of null packet to start of file.\n");
        }
    }
}

void FileSetHeader(struct DeliveryMethodInstance_t *this,
                        TSPacket_t *packets, int count)
{
    struct FileOutputInstance_t *instance = (struct FileOutputInstance_t*)this;
    fpos_t current;
    fgetpos(instance->fp, &current);

    rewind(instance->fp);

    if (fwrite(packets, TSPACKET_SIZE, count, instance->fp))
    {
        LogModule(LOG_INFO, FILEOUTPUT, "Failed to write all of packet to file.\n");
    }

    fsetpos(instance->fp, &current);
}
