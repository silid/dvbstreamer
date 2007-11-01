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

dr_73.h

Decode Default Authority Descriptor.

*/
#ifndef _DR_73_H
#define _DR_73_H

/*****************************************************************************
 * dvbpsi_content_id_dr_s
 *****************************************************************************/
/*!
 * \struct dvbpsi_default_authority_dr_s
 * \brief Default Authority Descriptor
 *
 * This structure is used to store a decoded Default Authority descriptor.
 */
/*!
 * \typedef struct dvbpsi_default_authority_dr_s dvbpsi_default_authority_dr_t
 * \brief dvbpsi_default_authority_dr_t type definition.
 */
typedef struct dvbpsi_default_authority_dr_s
{
    uint8_t authority[255];
}dvbpsi_default_authority_dr_t;

/*****************************************************************************
 * dvbpsi_DecodeLCNDr
 *****************************************************************************/
/*!
 * \fn dvbpsi_default_authority_dr_t *dvbpsi_DecodeDefaultAuthorityDr(dvbpsi_descriptor_t *p_descriptor)
 * \brief Decode a Default Authority descriptor (tag 0x73)
 * \param p_descriptor Raw descriptor to decode.
 * \return NULL if the descriptor could not be decoded or a pointer to a 
 *         dvbpsi_default_authority_dr_t structure.
 */
dvbpsi_default_authority_dr_t *dvbpsi_DecodeDefaultAuthorityDr(dvbpsi_descriptor_t *p_descriptor);

#endif



