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

dr_14.c

Decode Assocation Tag Descriptor.

*/
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "dvbpsi.h"
#include "../dvbpsi_private.h"
#include "descriptor.h"

#include "dr_14.h"


/*****************************************************************************
 * dvbpsi_DecodeAssociationTagDr
 *****************************************************************************/
dvbpsi_association_tag_dr_t *dvbpsi_DecodeAssociationTagDr(dvbpsi_descriptor_t *p_descriptor)
{
    dvbpsi_association_tag_dr_t *p_decoded;
    uint8_t selector_len;
    uint8_t private_data_len;
    
    /* Check the tag */
    if (p_descriptor->i_tag != 0x14)
    {
        return NULL;
    }

    /* Don't decode twice */
    if (p_descriptor->p_decoded)
    {
        return p_descriptor->p_decoded;
    }

    /* Check length */
    if (p_descriptor->i_length < 5)
    {
        return NULL;
    }
    
    selector_len = p_descriptor->p_data[4];

    /* Invalid selector length */
    if (selector_len + 5 > p_descriptor->i_length)
    {
        return NULL;
    }
    
    private_data_len= p_descriptor->i_length - (5 + selector_len);
    p_decoded = (dvbpsi_association_tag_dr_t*)malloc(sizeof(dvbpsi_association_tag_dr_t) + selector_len + private_data_len);
    if (!p_decoded)
    {
        return NULL;
    }

    p_decoded->i_tag = ((p_descriptor->p_data[0] & 0xff) << 8) | (p_descriptor->p_data[1] & 0xff);
    p_decoded->i_use = ((p_descriptor->p_data[2] & 0xff) << 8) | (p_descriptor->p_data[3] & 0xff);
    p_decoded->i_selector_len = selector_len;
    p_decoded->i_private_data_len= private_data_len;
    p_decoded->p_selector = ((void*)p_decoded) + sizeof(dvbpsi_association_tag_dr_t);
    p_decoded->p_private_data = p_decoded->p_selector + selector_len;
    memcpy(p_decoded->p_selector, &p_descriptor->p_data[5 ], selector_len);    
    memcpy(p_decoded->p_private_data, &p_descriptor->p_data[5 + selector_len], private_data_len);
    p_descriptor->p_decoded = (void*)p_decoded;

    return p_decoded;
}



