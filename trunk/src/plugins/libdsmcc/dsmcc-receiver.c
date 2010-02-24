/*
Copyright (C) 2010  Adam Charrett
based on libdsmcc by Richard Palmer

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

dsmcc-receiver.c

*/

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <zlib.h>

#include "dsmcc-receiver.h"
#include "dsmcc-descriptor.h"
#include "dsmcc-biop.h"
#include "dsmcc-cache.h"
#include "dsmcc-util.h"
#include "libdsmcc.h"


void dsmcc_init(struct dsmcc_status *status, const char *channel)
{
    int i;

    status->rec_files = status->total_files = 0;
    status->rec_dirs = status->total_dirs = 0;
    status->gzip_size = status->total_size = 0;

    for (i = 0;i < MAXCAROUSELS;i++)
    {
        status->carousels[i].streams = NULL;
        status->carousels[i].cache = NULL;
        status->carousels[i].filecache = malloc(sizeof(struct cache));
        status->carousels[i].gate = NULL;
        status->carousels[i].id = 0;
        dsmcc_cache_init(status->carousels[i].filecache, channel, status->debug_fd);
    }
}

void dsmcc_free(struct dsmcc_status *status)
{
    /* Free any carousel data and cached data.
     * TODO - actually cache on disk the cache data
     * TODO - more terrible memory madness, this all needs improving
     */
}

void dsmcc_add_stream(struct dsmcc_status *status, uint16_t tag)
{
    void DownloadSessionPIDAddTag(void *, uint16_t);
    DownloadSessionPIDAddTag(status->private, tag);
}

int dsmcc_process_section_header(struct dsmcc_section *section, unsigned char *Data, int Length)
{
    struct dsmcc_section_header *header = &section->sec;

    int crc_offset = 0;

    header->table_id = Data[0];

    header->flags[0] = Data[1];
    header->flags[1] = Data[2];

    /* Check CRC is set and private_indicator is set to its complement,
     * else skip packet */
    if (((header->flags[0] & 0x80) == 0) || (header->flags[0] & 0x40) != 0)
    {
        return 1; /* Section invalid */
    }

    /* Data[3] - reserved */

    header->table_id_extension = (Data[4] << 8) | Data[5];

    header->flags2 = Data[6];

    crc_offset = Length - 4 - 1;    /* 4 bytes */

    /* skip to end, read last 4 bytes and store in crc */

    header->crc = (Data[crc_offset] << 24) | (Data[crc_offset+1] << 16) |
                  (Data[crc_offset+2] << 8) | (Data[crc_offset+3]);

    return 0;
}

int dsmcc_process_msg_header(struct dsmcc_section *section, unsigned char *Data)
{
    struct dsmcc_message_header *header = &section->hdr.info;

    header->protocol = Data[0];

    if (header->protocol != 0x11)
    {
        return 1;
    }

//        fprintf(dsi_debug, "Protocol: %X\n", header->protocol);
    header->type = Data[1];
    if (header->type != 0x03)
    {
        return 1;
    }
//        fprintf(dsi_debug, "Type: %X\n", header->type);
    header->message_id = (Data[2] << 8) | Data[3];

//        fprintf(dsi_debug, "Message ID: %X\n", header->message_id);
    header->transaction_id = (Data[4] << 24) | (Data[5] << 16) |
                             (Data[6] << 8) | Data[7];
//        fprintf(dsi_debug, "Transaction ID: %lX\n", header->transaction_id);

    /* Data[8] - reserved */
    /* Data[9] - adapationLength 0x00 */

    header->message_len = (Data[10] << 8) | Data[11];
    if (header->message_len > 4076)  /* Beyond valid length */
    {
        return 1;
    }

//        fprintf(dsi_debug, "Message Length: %d\n", header->message_len);

    return 0;
}

int dsmcc_process_section_gateway(struct dsmcc_status *status, unsigned char *Data, int Length, int pid)
{
    int off = 0, ret, i;
    struct obj_carousel *car;
    struct stream *str;

    /* Find which object carousel this pid's data belongs to */

    for (i = 0;i < MAXCAROUSELS;i++)
    {
        car = &status->carousels[i];
        for (str = car->streams;str != NULL;str = str->next)
        {
            if (str->pid == pid)
            {
                break;
            }
        }
        if (str != NULL)
        {
            if (car->gate != NULL)  /* TODO check gate version not changed */
            {
                return 0; /* We already have gateway */
            }
            else
            {
                break;
            }
        }
    }

//     syslog(LOG_ERR, ("Setting gateway for pid %d", pid);
    if (status->debug_fd != NULL)
    {
        fprintf(status->debug_fd, "[libdsmcc] Setting gateway for pid %d\n", pid);
    }

    if (car == NULL)
    {
        if (status->debug_fd != NULL)
        {
            fprintf(status->debug_fd, "[libdsmcc] Gateway for unknown carouse\n");
        }
        return 0;
    }

    car->gate = (struct dsmcc_dsi *)malloc(sizeof(struct dsmcc_dsi));

    /* 0-19 Server id = 20 * 0xFF */

    /* 20,21 compatibilydescriptorlength = 0x0000 */

    off = 22;
    car->gate->data_len = (Data[off] << 8) | Data[off+1];

    off += 2;
//        fprintf(dsi_debug, "Data Length: %d\n", car->gate->data_len);

    /* does not even exist ?
    gate->num_groups = (Data[off] << 8) | Data[off+1];
    off+=2;
    fprintf(dsi_debug, "Num. Groups: %d\n", gate->num_groups);
    */

    /* TODO - process groups ( if ever exist ? ) */

//      fprintf(dsi_debug, "Processing BiopBody...\n");
    ret = dsmcc_biop_process_ior(&car->gate->profile, Data + DSMCC_BIOP_OFFSET);
    if (ret > 0)
    {
        off += ret;
    }
    else
        { /* TODO error */ }

    /* Set carousel id if not already given in data_broadcast_id_descriptor
       (only teletext doesnt bother with this ) */

    if (car->id == 0)   /* TODO is carousel id 0 ever valid ? */
    {
        car->id = car->gate->profile.body.full.obj_loc.carousel_id;
    }

    if (status->debug_fd != NULL)
    {
        fprintf(status->debug_fd, "[libdsmcc] Gateway Module %d on carousel %ld\n", car->gate->profile.body.full.obj_loc.module_id, car->id);
    }

    dsmcc_add_stream(status, car->gate->profile.body.full.dsm_conn.tap.assoc_tag);

    /* skip taps and context */

    off += 2;

    /* TODO process descriptors */
    car->gate->user_data_len = Data[off++];
    if (car->gate->user_data_len > 0)
    {
        car->gate->user_data = (unsigned char *)malloc(car->gate->data_len);
        memcpy(car->gate->user_data, Data + off, car->gate->data_len);
    }

    /*        fprintf(dsi_debug, "BiopBody - Data Length %ld\n",
       car->gate->profile.body.full.data_len);

            fprintf(dsi_debug, "BiopBody - Lite Components %d\n",
       car->gate->profile.body.full.lite_components_count);
    */

    return 0;
}

int dsmcc_process_section_info(struct dsmcc_status *status, struct dsmcc_section *section, unsigned char *Data, int Length)
{
    struct dsmcc_dii *dii = &section->msg.dii;
    struct obj_carousel *car = NULL;
    int off = 0, i, ret;

    dii->download_id = (Data[0] << 24) | (Data[1] << 16) |
                       (Data[2] << 8)  | (Data[3]) ;

    for (i = 0;i < MAXCAROUSELS;i++)
    {
        car = &status->carousels[i];
        if (car->id == dii->download_id)
        {
            break;
        }
    }

    if (car == NULL)
    {
        if (status->debug_fd != NULL)
        {
            fprintf(status->debug_fd, "[libdsmcc] Section Info for unknown carousel %ld\n", dii->download_id);

        }
        /* No known carousels yet (possible?) TODO ! */
        return 1;
    }

//      fprintf(dsi_debug, "Info -> Download ID = %lX\n", dii->download_id);

    off += 4;
    dii->block_size = Data[off] << 8 | Data[off+1];
//      fprintf(dsi_debug, "Info -> Block Size = %d\n", dii->block_size);
    off += 2;

    off += 6; /* not used fields */

    dii->tc_download_scenario = (Data[off] << 24) | (Data[off+1] << 16) |
                                (Data[off+2] << 8) | Data[off+3];
    /*
            fprintf(dsi_debug, "Info -> tc download scenario = %ld\n",
          dii->tc_download_scenario);
    */
    off += 4;

    /* skip unused compatibility descriptor len */

    off += 2;

    dii->number_modules = (Data[off] << 8) | Data[off+1];

//      fprintf(dsi_debug, "Info -> number modules = %d\n",dii->number_modules);
    off += 2;

    dii->modules = (struct dsmcc_module_info*)
                   malloc(sizeof(struct dsmcc_module_info) * dii->number_modules);

    for (i = 0; i < dii->number_modules; i++)
    {
        dii->modules[i].module_id = (Data[off] << 8) | Data[off+1];
        off += 2;
        dii->modules[i].module_size = (Data[off] << 24) |
                                      (Data[off+1] << 16) |
                                      (Data[off+2] << 8) |
                                      Data[off+3];
        off += 4;
        dii->modules[i].module_version = Data[off++];
        dii->modules[i].module_info_len = Data[off++];

        if (status->debug_fd != NULL)
        {
            fprintf(status->debug_fd, "[libdsmcc] Module %d -> Size = %ld Version = %d\n", dii->modules[i].module_id, dii->modules[i].module_size, dii->modules[i].module_version);
        }
        ret = dsmcc_biop_process_module_info(&dii->modules[i].modinfo,
                                             Data + off);

        if (ret > 0)
        {
            off += ret;
        }
        else
            { /* TODO error */ }
    }

    dii->private_data_len = (Data[off] << 8) | Data[off+1];

    /*        fprintf(dsi_debug, "Info -> Private Data Length = %d\n",
          dii->private_data_len);
    */
    /* UKProfile - ignored
    dii->private_data = (char *)malloc(dii->private_data_len);
    memcpy(dii->private_data, Data+off, dii->private_data_len);
    */

    /* TODO add module info within this function */

    dsmcc_add_module_info(status, section, car);

    /* Free most of the memory up... all that effort for nothing */

    for (i = 0; i < dii->number_modules; i++)
    {
        if (dii->modules[i].modinfo.tap.selector_len > 0)
            free(dii->modules[i].modinfo.tap.selector_data);

    }

    free(dii->modules); /* TODO clean up properly... done? */

    return 0;
}

void dsmcc_process_section_indication(struct dsmcc_status *status, unsigned char *Data, int Length, int pid)
{
    struct dsmcc_section section;
    int ret;

    ret = dsmcc_process_section_header(&section, Data + DSMCC_SECTION_OFFSET, Length);

    if (ret != 0)
    {
        fprintf(status->debug_fd, "[libdsmcc] Indication Section Header error");
        return;
    }

    ret = dsmcc_process_msg_header(&section, Data + DSMCC_MSGHDR_OFFSET);

    if (ret != 0)
    {
        fprintf(status->debug_fd, "[libdsmcc] Indication Msg Header error");
        return;
    }

    if (section.hdr.info.message_id == 0x1006)
    {
        if (status->debug_fd != NULL)
        {
            fprintf(status->debug_fd, "[libdsmcc] Server Gateway\n");
        }
        dsmcc_process_section_gateway(status, Data + DSMCC_DSI_OFFSET, Length, pid);
    }
    else if (section.hdr.info.message_id == 0x1002)
    {
        if (status->debug_fd != NULL)
        {
            fprintf(status->debug_fd, "[libdsmcc] Module Info\n");
        }
        dsmcc_process_section_info(status, &section, Data + DSMCC_DII_OFFSET, Length);
    }
    else
    {
        /* Error */
    }

}


void dsmcc_add_module_info(struct dsmcc_status *status, struct dsmcc_section *section, struct obj_carousel *car)
{
    int i, num_blocks, found;
    struct cache_module_data *cachep = car->cache;
    struct descriptor *desc, *last;
    struct dsmcc_dii *dii = &section->msg.dii;

    /* loop through modules and add to cache list if no module with
     * same id or a different version. */

    for (i = 0; i < dii->number_modules; i++)
    {
        found = 0;
        for (;cachep != NULL;cachep = cachep->next)
        {
            if (cachep->carousel_id == dii->download_id &&
                    cachep->module_id == dii->modules[i].module_id)
            {
                /* already known */
                if (cachep->version == dii->modules[i].module_version)
                {
//   fprintf(dsi_debug, "AddModuleInfo -> Already Know Module %d\n", dii->modules[i].module_id);
                    if (status->debug_fd != NULL)
                    {
                        fprintf(status->debug_fd, "[libdsmcc] Already Know Module %d\n", dii->modules[i].module_id);
                    }
                    found =  1;
                    break;
                }
                else
                {
                    /* Drop old data */
                    if (status->debug_fd != NULL)
                    {
                        fprintf(status->debug_fd, "[libdsmcc] Updated Module %d\n", dii->modules[i].module_id);
                    }
                    if (cachep->descriptors != NULL)
                    {
                        desc = cachep->descriptors;
                        while (desc != NULL)
                        {
                            last = desc;
                            desc = desc->next;
                            dsmcc_desc_free(last);
                        }
                    }
                    if (cachep->data != NULL)
                    {
                        free(cachep->data);
                    }
                    if (cachep->prev != NULL)
                    {
                        cachep->prev->next = cachep->next;
                        if (cachep->next != NULL)
                        {
                            cachep->next->prev = cachep->prev;
                        }
                    }
                    else
                    {
                        car->cache = cachep->next;
                        if (cachep->next != NULL)
                        {
                            cachep->next->prev = NULL;
                        }
                    }
                    free(cachep);
                    break;
                }
            }
        }

        if (found == 0)
        {
            if (status->debug_fd != NULL)
            {
                fprintf(status->debug_fd, "[libdsmcc] Saving info for module %d\n", dii->modules[i].module_id);
            }
            if (car->cache != NULL)
            {
                for (cachep = car->cache;
                        cachep->next != NULL;cachep = cachep->next)
                {
                    ;
                }
                cachep->next = (struct cache_module_data *)malloc(sizeof(struct cache_module_data));
                cachep->next->prev = cachep;
                cachep = cachep->next;
            }
            else
            {
                car->cache = (struct cache_module_data *)malloc(sizeof(struct cache_module_data));
                cachep = car->cache;
                cachep->prev = NULL;
            }

            cachep->carousel_id = dii->download_id;
            cachep->module_id = dii->modules[i].module_id;
            cachep->version = dii->modules[i].module_version;
            cachep->size = dii->modules[i].module_size;
            cachep->curp = cachep->block_num = 0;
            num_blocks = cachep->size / dii->block_size;
            if ((cachep->size % dii->block_size) != 0)
                num_blocks++;
            cachep->bstatus = (char*)malloc(((num_blocks / 8) + 1) * sizeof(char));
            bzero(cachep->bstatus, (num_blocks / 8) + 1);
            /*  syslog(LOG_ERR, "Allocated %d bytes to store status for module %d",
                (num_blocks/8)+1, cachep->module_id);
             */
            cachep->data = NULL;
            cachep->next = NULL;
            cachep->blocks = NULL;
            cachep->tag = dii->modules[i].modinfo.tap.assoc_tag;
            dsmcc_add_stream(status, cachep->tag);
            /* Steal the descriptors  TODO this is very bad... */
            cachep->descriptors = dii->modules[i].modinfo.descriptors;
            dii->modules[i].modinfo.descriptors = NULL;
            cachep->cached = 0;
        }
    }

}

int dsmcc_process_data_header(struct dsmcc_section *section, unsigned char *Data, int Length)
{
    struct dsmcc_data_header *hdr = &section->hdr.data;

    hdr->protocol = Data[0];
// fprintf(dsi_debug, "Data -> Header - > Protocol %d\n", hdr->protocol);

    hdr->type = Data[1];
// fprintf(dsi_debug, "Data -> Header - > Type %d\n", hdr->type);

    hdr->message_id = (Data[2] << 8) | Data[3];
// fprintf(dsi_debug, "Data -> Header - > MessageID %d\n",hdr->message_id);

    hdr->download_id = (Data[4] << 24) | (Data[5] << 16) |
                       (Data[6] << 8) | Data[7];
    /* fprintf(dsi_debug, "Data -> Header - > DownloadID %ld\n",
        hdr->download_id);
    */
    /* skip reserved byte */

    hdr->adaptation_len = Data[9];

// fprintf(dsi_debug, "Data -> Header - > Adaption Len %d\n", hdr->adaptation_len);

    hdr->message_len = (Data[10] << 8) | Data[11];
// fprintf(dsi_debug, "Data -> Header - > Message Len %d\n", hdr->message_len);

    /* TODO adapationHeader ?? */

    return 0;
}

int dsmcc_process_section_block(struct dsmcc_status *status, struct dsmcc_section *section, unsigned char *Data, int Length)
{
    struct dsmcc_ddb *ddb = &section->msg.ddb;

    ddb->module_id = (Data[0] << 8) | Data[1];

// fprintf(dsi_debug, "Data -> Block - > Module ID %u\n", ddb->module_id);

    ddb->module_version = Data[2];

// fprintf(dsi_debug, "Data -> Block - > Module Version %u\n", ddb->module_version);

    /* skip reserved byte */

    ddb->block_number = (Data[4] << 8) | Data[5];

// fprintf(dsi_debug,"Data -> Block - > Block Num %u\n",ddb->block_number);

    ddb->len = section->hdr.data.message_len - 6;

    ddb->next = NULL; /* Not used here, used to link all data blocks
       in order in AddModuleData. Hmmm. */
    ddb->blockdata = NULL;

    if (status->debug_fd != NULL)
    {
        fprintf(status->debug_fd, "[libdsmcc] Data Block ModID %d Pos %d Version %d\n", ddb->module_id, ddb->block_number, ddb->module_version);
    }

    dsmcc_add_module_data(status, section, Data + 6);

    return 0;
}


void dsmcc_process_section_data(struct dsmcc_status *status, unsigned char *Data, int Length)
{
    struct dsmcc_section *section;

    section = (struct dsmcc_section *)malloc(sizeof(struct dsmcc_section));

// fprintf(dsi_debug, "Reading section header\n");
    dsmcc_process_section_header(section, Data + DSMCC_SECTION_OFFSET, Length);

// fprintf(dsi_debug, "Reading data header\n");
    dsmcc_process_data_header(section, Data + DSMCC_DATAHDR_OFFSET, Length);

// fprintf(dsi_debug, "Reading data \n");
    dsmcc_process_section_block(status, section, Data + DSMCC_DDB_OFFSET, Length);

    free(section);
}

void dsmcc_add_module_data(struct dsmcc_status *status, struct dsmcc_section *section, unsigned char *Data)
{
    int i, ret, found = 0;
    unsigned char *data = NULL;
    unsigned long data_len = 0;
    struct cache_module_data *cachep = NULL;
    struct descriptor *desc = NULL;
    struct dsmcc_ddb *lb, *pb, *nb, *ddb = &section->msg.ddb;
    struct obj_carousel *car;

    i = ret = 0;

    /* Scan through known modules and append data */

    for (i = 0;i < MAXCAROUSELS;i++)
    {
        car = &status->carousels[i];
        if (car->id == section->hdr.data.download_id)
        {
            break;
        }
    }

    if (car == NULL)
    {
        if (status->debug_fd != NULL)
        {
            fprintf(status->debug_fd, "[libdsmcc] Data block for module in unknown carousel %ld", section->hdr.data.download_id);
        }
        /* TODO carousel not yet known! is this possible ? */
        return;
    }

    if (status->debug_fd != NULL)
    {
        fprintf(status->debug_fd, "[libdsmcc] Data block on carousel %ld\n", car->id);
    }

    cachep = car->cache;

    for (; cachep != NULL; cachep = cachep->next)
    {
        if (cachep->carousel_id == section->hdr.data.download_id &&
                cachep->module_id == ddb->module_id)
        {
            found = 1;
            /*  fprintf(dsi_debug, "Found linking module (%d)...\n",
                 ddb->module_id);
            */
            break;
        }
    }

    if (found == 0)
    {
        return;    /* Not found module info */
    }

    if (cachep->version == ddb->module_version)
    {
        if (cachep->cached)
        {
            if (status->debug_fd != NULL)
            {
                fprintf(status->debug_fd, "[libdsmcc] Cached complete module already %d\n", cachep->module_id);
            }
            return;/* Already got it */
        }
        else
        {
            /* Check if we have this block already or not. If not append
             * to list
             */
            if (BLOCK_GOT(cachep->bstatus, ddb->block_number) == 0)
            {
                if ((cachep->blocks == NULL) || (cachep->blocks->block_number > ddb->block_number))
                {
                    nb = cachep->blocks; /* NULL or previous first in list */
                    cachep->blocks = (struct dsmcc_ddb*)malloc(sizeof(struct dsmcc_ddb));
                    lb = cachep->blocks;
                }
                else
                {
                    for (pb = lb = cachep->blocks;
                            (lb != NULL) && (lb->block_number < ddb->block_number);
                            pb = lb, lb = lb->next)
                    {
                        ;
                    }

                    nb = pb->next;
                    pb->next = (struct dsmcc_ddb*)malloc(sizeof(struct dsmcc_ddb));
                    lb = pb->next;
                }

                lb->module_id = ddb->module_id;
                lb->module_version = ddb->module_version;
                lb->block_number = ddb->block_number;
                lb->blockdata = (unsigned char*)malloc(ddb->len);
                memcpy(lb->blockdata, Data, ddb->len);
                lb->len = ddb->len;
                cachep->curp += ddb->len;
                lb->next = nb;
                BLOCK_SET(cachep->bstatus, ddb->block_number);
            }
        }

        if (status->debug_fd != NULL)
        {
            fprintf(status->debug_fd, "[libdsmcc] Module %d Current Size %lu Total Size %lu\n", cachep->module_id, cachep->curp, cachep->size);
        }

        if (cachep->curp >= cachep->size)
        {
            if (status->debug_fd != NULL)
            {
                fprintf(status->debug_fd, "[libdsmcc] Reconstructing module %d from blocks\n", cachep->module_id);
            }
            /* Re-assemble the blocks into the complete module */
            cachep->data = (unsigned char*)malloc(cachep->size);
            cachep->curp = 0;
            pb = lb = cachep->blocks;
            while (lb != NULL)
            {
                memcpy(cachep->data + cachep->curp, lb->blockdata, lb->len);
                cachep->curp += lb->len;

                pb = lb;
                lb = lb->next;

                if (pb->blockdata != NULL)
                    free(pb->blockdata);
                free(pb);
            }
            cachep->blocks = NULL;

            /* Uncompress.... TODO - scan for compressed descriptor */
            for (desc = cachep->descriptors;desc != NULL; desc = desc->next)
            {
                if (desc && (desc->tag == 0x09))
                {
                    break;
                }
            }
            if (desc != NULL)
            {
//            syslog(LOG_ERR, "Uncompressing...(%lu bytes compressed - %lu bytes memory", cachep->curp, desc->data.compressed.original_size);

                data_len = desc->data.compressed.original_size + 1;
                data = (unsigned char *)malloc(data_len + 1);
//     syslog(LOG_ERR, "Compress data memory %p - %p (%ld bytes)", cachep->data, cachep->data+cachep->size, cachep->size);
//     syslog(LOG_ERR, "Uncompress data memory %p - %p (%ld bytes)", data, data+data_len, data_len);

//     fprintf(dsi_debug, "(set %lu ", data_len);
//     syslog(LOG_ERR, "(set %lu", data_len);
                ret = uncompress(data, &data_len, cachep->data, cachep->size);
//     syslog(LOG_ERR, "expected %lu real %lu ret %d)", cachep->size, data_len, ret);

                if (ret == Z_DATA_ERROR)
                {
                    if (status->debug_fd != NULL)
                    {
                        fprintf(status->debug_fd, "[libdsmcc] compression error - invalid data, skipping\n");
                    }
                    if (data != NULL)
                    {
                        free(data);
                    }
                    cachep->curp = 0;
                    if (cachep->data != NULL)
                    {
                        free(cachep->data);
                        cachep->data = NULL;
                    }
                    return;
                }
                else if (ret == Z_BUF_ERROR)
                {
                    if (status->debug_fd != NULL)
                    {
                        fprintf(status->debug_fd, "[libdsmcc] compression error - buffer error, skipping\n");
                    }
                    if (data != NULL)
                    {
                        free(data);
                    }
                    cachep->curp = 0;
                    if (cachep->data != NULL)
                    {
                        free(cachep->data);
                        cachep->data = NULL;
                    }
                    return;
                }
                else if (ret == Z_MEM_ERROR)
                {
                    if (status->debug_fd != NULL)
                    {
                        fprintf(status->debug_fd, "[libdsmcc] compression error - out of mem, skipping\n");
                    }
                    if (data != NULL)
                    {
                        free(data);
                    }
                    cachep->curp = 0;
                    if (cachep->data != NULL)
                    {
                        free(cachep->data);
                        cachep->data = NULL;
                    }
                    return;
                }
                if (cachep->data != NULL)
                {
                    free(cachep->data);
                }
                cachep->data = data;
                if (status->debug_fd != NULL)
                {
                    fprintf(status->debug_fd, "[libdsmcc] Processing data\n");
                }
                // Return list of streams that directory needs
                dsmcc_biop_process_data(car->filecache, cachep);
                cachep->cached = 1;
            }
            else
            {
                /* not compressed */
                if (status->debug_fd != NULL)
                {
                    fprintf(status->debug_fd, "[libdsmcc] Processing data (uncompressed)\n");
                }
                // Return list of streams that directory needs
                dsmcc_biop_process_data(car->filecache, cachep);
                cachep->cached = 1;
            }
        }
    }
}

void dsmcc_process_section_desc(unsigned char *Data, int Length)
{
    struct dsmcc_section section;
    int ret;

    ret = dsmcc_process_section_header(&section, Data + DSMCC_SECTION_OFFSET, Length);

    /* TODO */

}

void dsmcc_process_section(struct dsmcc_status *status, unsigned char *Data, int Length, int pid)
{
    unsigned long crc32_decode;
    unsigned short section_len;

    /* Check CRC before trying to parse */

    section_len = ((Data[1] & 0xF) << 8) | (Data[2]) ;
    section_len += 3;/* 3 bytes before length count starts */

    crc32_decode = dsmcc_crc32(Data, section_len);

// fprintf(dsi_debug, "Length %d CRC - %lX \n",section_len, crc32_decode);

    if (crc32_decode != 0)
    {
        fprintf(status->debug_fd, "[libdsmcc] Corrupt CRC for section, dropping");
        if (status->debug_fd != NULL)
        {
            FILE *crcfd;
            fprintf(status->debug_fd, "[libdsmcc] Dropping corrupt section (Got %lX\n", crc32_decode);
            fprintf(status->debug_fd, "[libdsmcc] Written packet to crc-error.ts\n");
            crcfd = fopen("crc-error.ts", "w");
            fwrite(Data, 1, Length, crcfd);
            fclose(crcfd);
        }
        return;
    }

    switch (Data[0])
    {
        case DSMCC_SECTION_INDICATION:
            if (status->debug_fd != NULL)
            {
                fprintf(status->debug_fd, "[libdsmcc] Server/Info Section\n");
            }
            dsmcc_process_section_indication(status, Data, Length, pid);
            break;
        case DSMCC_SECTION_DATA:
            if (status->debug_fd != NULL)
            {
                fprintf(status->debug_fd, "[libdsmcc] Data Section\n");
            }
            dsmcc_process_section_data(status, Data, Length);
            break;
        case DSMCC_SECTION_DESCR:
            if (status->debug_fd != NULL)
            {
                fprintf(status->debug_fd, "[libdsmcc] Descriptor Section\n");
            }
            dsmcc_process_section_desc(Data, Length);
            break;
        default:
            break;
    }

// fclose(dsi_debug);

}
