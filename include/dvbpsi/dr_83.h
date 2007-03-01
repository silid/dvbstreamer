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

dr_83.h

Decode Logical Channel Number Descriptor.

*/
#ifndef _DR_83_H
#define _DR_83_H

typedef struct dvbpsi_lcn_entry_s
{
    uint16_t i_service_id;
    int      b_visible_service_flag;
    uint16_t i_logical_channel_number;
}dvbpsi_lcn_entry_t;

typedef struct dvbpsi_lcn_dr_s
{
    uint8_t i_number_of_entries;
    dvbpsi_lcn_entry_t p_entries[64];
}dvbpsi_lcn_dr_t;

dvbpsi_lcn_dr_t *dvbpsi_DecodeLCNDr(dvbpsi_descriptor_t *p_descriptor);

#endif

