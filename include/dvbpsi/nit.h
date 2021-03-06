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

nitdecoder.h

Decode Network Information Tables.

*/
#ifndef _NIT_H
#define _NIT_H
#define PID_NIT 0x0010
#define TABLE_ID_NIT_ACTUAL 0x40
#define TABLE_ID_NIT_OTHER  0x41

/*****************************************************************************
 * dvbpsi_nit_service_t
 *****************************************************************************/
/*!
 * \struct dvbpsi_nit_service_s
 * \brief NIT service description structure.
 *
 * This structure is used to store a decoded NIT service description.
 * (ETSI EN 300 468 V1.4.1 section 5.2.3).
 */
/*!
 * \typedef struct dvbpsi_nit_transport_s dvbpsi_nit_transport_t
 * \brief dvbpsi_nit_transport_t type definition.
 */
typedef struct dvbpsi_nit_transport_s
{
  uint16_t                  i_ts_id;                /*!< transport_stream_id */
  uint16_t                  i_original_network_id;  /*!< original_network_id */
  dvbpsi_descriptor_t *     p_first_descriptor;     /*!< First of the following
                                                         DVB descriptors */

  struct dvbpsi_nit_transport_s * p_next;             /*!< next element of
                                                             the list */

} dvbpsi_nit_transport_t;


/*****************************************************************************
 * dvbpsi_nit_t
 *****************************************************************************/
/*!
 * \struct dvbpsi_nit_s
 * \brief NIT structure.
 *
 * This structure is used to store a decoded NIT.
 * (ETSI EN 300 468 V1.4.1 section 5.2.1).
 */
/*!
 * \typedef struct dvbpsi_nit_s dvbpsi_nit_t
 * \brief dvbpsi_nit_t type definition.
 */
typedef struct dvbpsi_nit_s
{
  int                       b_actual;           /*!< TRUE if NIT(Actual) or FALSE if NIT(Other)*/
  uint16_t                  i_network_id;       /*!< network_id */
  uint8_t                   i_version;          /*!< version_number */
  int                       b_current_next;     /*!< current_next_indicator */

  dvbpsi_descriptor_t *     p_first_descriptor; /*!< First of the following Network descriptors */

  dvbpsi_nit_transport_t *  p_first_transport;  /*!< First of the following transports */
} dvbpsi_nit_t;


/*****************************************************************************
 * dvbpsi_nit_callback
 *****************************************************************************/
/*!
 * \typedef void (* dvbpsi_nit_callback)(void* p_cb_data,
                                         dvbpsi_nit_t* p_new_nit)
 * \brief Callback type definition.
 */
typedef void (* dvbpsi_nit_callback)(void* p_cb_data, dvbpsi_nit_t* p_new_nit);


/*****************************************************************************
 * dvbpsi_AttachNIT
 *****************************************************************************/
/*!
 * \fn void dvbpsi_AttachNIT(dvbpsi_demux_t * p_demux, uint8_t i_table_id,
          uint16_t i_extension, dvbpsi_nit_callback pf_callback,
                               void* p_cb_data)
 * \brief Creation and initialization of a NIT decoder.
 * \param p_demux Subtable demultiplexor to which the decoder is attached.
 * \param i_table_id Table ID, 0x40 or 0x41.
 * \param i_extension Table ID extension, here Network ID.
 * \param pf_callback function to call back on new NIT.
 * \param p_cb_data private data given in argument to the callback.
 * \return 0 if everything went ok.
 */
int dvbpsi_AttachNIT(dvbpsi_decoder_t * p_psi_decoder, uint8_t i_table_id,
          uint16_t i_extension, dvbpsi_nit_callback pf_callback,
                               void* p_cb_data);


/*****************************************************************************
 * dvbpsi_DetachNIT
 *****************************************************************************/
/*!
 * \fn void dvbpsi_DetachNIT(dvbpsi_demux_t * p_demux, uint8_t i_table_id,
          uint16_t i_extension)
 * \brief Destroy a NIT decoder.
 * \param p_demux Subtable demultiplexor to which the decoder is attached.
 * \param i_table_id Table ID, 0x42 or 0x46.
 * \param i_extension Table ID extension, here TS ID.
 * \return nothing.
 */
void dvbpsi_DetachNIT(dvbpsi_demux_t * p_demux, uint8_t i_table_id,
          uint16_t i_extension);


/*****************************************************************************
 * dvbpsi_InitNIT/dvbpsi_NewNIT
 *****************************************************************************/
/*!
 * \fn void dvbpsi_InitNIT(dvbpsi_nit_t* p_nit, uint16_t i_network_id,
          uint8_t i_version, int b_current_next)
 * \brief Initialize a user-allocated dvbpsi_nit_t structure.
 * \param p_nit pointer to the NIT structure
 * \param b_actual True if this is the NIT(actual), false if NIT(other)
 * \param i_network_id network id
 * \param i_version NIT version
 * \param b_current_next current next indicator
 * \return nothing.
 */
void dvbpsi_InitNIT(dvbpsi_nit_t *p_nit, int b_actual, uint16_t i_network_id, 
                    uint8_t i_version, int b_current_next);

/*!
 * \def dvbpsi_NewNIT(p_nit, i_network_id, i_version, b_current_next)
 * \brief Allocate and initialize a new dvbpsi_nit_t structure.  Use ObjectRefDec to release delete it.
 * \param p_nit pointer to the NIT structure
 * \param i_network_id network id
 * \param i_version NIT version
 * \param b_current_next current next indicator
 * \return nothing.
 */
#define dvbpsi_NewNIT(p_nit, b_actual, i_network_id, i_version, b_current_next) \
do {                                                                    \
  ObjectRegisterTypeDestructor(dvbpsi_nit_t, (ObjectDestructor_t)dvbpsi_EmptyNIT);          \
  p_nit = (dvbpsi_nit_t*)ObjectCreateType(dvbpsi_nit_t);                \
  if(p_nit != NULL)                                                     \
    dvbpsi_InitNIT(p_nit, b_actual, i_network_id, i_version, b_current_next); \
} while(0);


/*****************************************************************************
 * dvbpsi_EmptyNIT
 *****************************************************************************/
/*!
 * \fn void dvbpsi_EmptyNIT(dvbpsi_nit_t* p_nit)
 * \brief Clean a dvbpsi_nit_t structure.
 * \param p_nit pointer to the NIT structure
 * \return nothing.
 */
void dvbpsi_EmptyNIT(dvbpsi_nit_t *p_nit);

#endif
