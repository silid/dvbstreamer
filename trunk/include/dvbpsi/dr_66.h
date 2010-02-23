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

dr_62.h

Decode Frequency List Descriptor.

*/
#ifndef _DR_62_H
#define _DR_62_H

/*****************************************************************************
 * dvbpsi_data_broadcast_id_dr_s
 *****************************************************************************/
/*!
 * \struct dvbpsi_data_broadcast_id_dr_s
 * \brief Data Broadcast id Descriptor
 *
 * This structure is used to store a decoded Data Broadcast id descriptor.
 */
/*!
 * \typedef struct dvbpsi_data_broadcast_id_dr_s dvbpsi_data_broadcast_id_dr_t
 * \brief dvbpsi_data_broadcast_id_dr_t type definition.
 */
typedef struct dvbpsi_data_broadcast_id_dr_s
{
    uint16_t i_data_broadcast_id;
    uint8_t i_id_selector_len;
    uint8_t s_id_selector[0];
}dvbpsi_data_broadcast_id_dr_t;

/*****************************************************************************
 * dvbpsi_DecodeDataBroadcastIdDr
 *****************************************************************************/
/*!
 * \fn dvbpsi_data_broadcast_id_dr_t *dvbpsi_DecodeDataBroadcastIdDr(
 *        dvbpsi_descriptor_t *p_descriptor)
 * \brief Decode a Data broadcast id descriptor (tag 0x66)
 * \param p_descriptor Raw descriptor to decode.
 * \return NULL if the descriptor could not be decoded or a pointer to a 
 *         dvbpsi_data_broadcast_id_dr_t structure.
 */
dvbpsi_data_broadcast_id_dr_t *dvbpsi_DecodeDataBroadcastIdDr(dvbpsi_descriptor_t *p_descriptor);

#endif



