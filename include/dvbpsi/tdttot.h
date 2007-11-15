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

tdttot.h

Time Date Table and Time Offset Table.

*/
#ifndef _TDTTOT_H
#define _TDTTOT_H

/*****************************************************************************
 * dvbpsi_tdt_t
 *****************************************************************************/
/*!
 * \struct dvbpsi_tdt_tot_t
 * \brief TDT/TOT structure.
 *
 * This structure is used to store a decoded TDT/TOT.
 * (ETSI EN 300 468 V1.4.1 section 5.2.5/6).
 */
/*!
 * \typedef struct dvbpsi_tdt_tot_s dvbpsi_tdt_tot_t
 * \brief dvbpsi_tdt_tot_t type definition.
 */
typedef struct dvbpsi_tdt_tot_s
{
    dvbpsi_date_time_t      t_date_time; /*!< UTC Date/Time */
    dvbpsi_descriptor_t*    p_first_descriptor; /*!< TOT descriptors, only present if the table was a TOT */
} dvbpsi_tdt_tot_t;


/*****************************************************************************
 * dvbpsi_tdt_tot_callback
 *****************************************************************************/
/*!
 * \typedef void (* dvbpsi_tdt_tot_callback)(void* p_cb_data,
                                         dvbpsi_tdt_tot_t* p_new_tdt_tot)
 * \brief Callback type definition.
 */
typedef void (* dvbpsi_tdt_tot_callback)(void* p_cb_data, dvbpsi_tdt_tot_t* p_new_tdt_tot);



/*****************************************************************************
 * dvbpsi_AttachTDTTOT
 *****************************************************************************/
/*!
 * \fn void dvbpsi_AttachTDTTOT(dvbpsi_tdt_tot_callback pf_callback,
                               void* p_cb_data)
 * \brief Creation and initialization of a TDT/TOT decoder.
 * \param pf_callback function to call back on new NIT.
 * \param p_cb_data private data given in argument to the callback.
 * \return a pointer to the decoder for future calls.
 */
dvbpsi_handle dvbpsi_AttachTDTTOT(dvbpsi_tdt_tot_callback pf_callback, void* p_cb_data);


/*****************************************************************************
 * dvbpsi_DetachTDTTOT
 *****************************************************************************/
/*!
 * \fn void dvbpsi_DetachTDTTOT(dvbpsi_handle h_dvbpsi)
 * \brief Destroy a TDT/TOT decoder.
 * \param h_dvbpsi handle to the decoder
 * \return nothing.
 *
 * The handle isn't valid any more.
 */
void dvbpsi_DetachTDTTOT(dvbpsi_handle h_dvbpsi);


/*****************************************************************************
 * dvbpsi_InitTDTTOT/dvbpsi_NewTDTTOT
 *****************************************************************************/
/*!
 * \fn void dvbpsi_InitTDTTOT(dvbpsi_nit_t* p_nit, uint16_t i_network_id,
          uint8_t i_version, int b_current_next)
 * \brief Initialize a user-allocated dvbpsi_tdt_tot_t structure.
 * \param p_tdt_tot pointer to the TDT/TOT structure
 * \param i_year Year in UTC
 * \param i_month Month in UTC
 * \param i_day Day in UTC
 * \param i_hour Hour in UTC
 * \param i_minute Minute in UTC
 * \param i_second Second in UTC
 * \return nothing.
 */
void dvbpsi_InitTDTTOT(dvbpsi_tdt_tot_t *p_tdt_tot,
                        int i_year, int i_month, int i_day,
                        int i_hour, int i_minute, int i_second);

/*!
 * \def dvbpsi_NewTDTTOT(p_nit, i_network_id, i_version, b_current_next)
 * \brief Allocate and initialize a new dvbpsi_nit_t structure.
 * \param p_tdt_tot pointer to the TDT/TOT structure
 * \param i_year Year in UTC
 * \param i_month Month in UTC
 * \param i_day Day in UTC
 * \param i_hour Hour in UTC
 * \param i_minute Minute in UTC
 * \param i_second Second in UTC
 * \return nothing.
 */
#define dvbpsi_NewTDTTOT(p_tdt_tot, i_year, i_month, i_day,         \
                         i_hour, i_minute, i_second)                \
do {                                                                \
  ObjectRegisterTypeDestructor(dvbpsi_tdt_tot_t, (ObjectDestructor_t)dvbpsi_EmptyTDTTOT);\
  p_tdt_tot = (dvbpsi_tdt_tot_t*)ObjectCreateType(dvbpsi_tdt_tot_t);\
  if(p_tdt_tot != NULL)                                             \
    dvbpsi_InitTDTTOT(p_tdt_tot, i_year, i_month, i_day,            \
                      i_hour, i_minute, i_second);                  \
} while(0);


/*****************************************************************************
 * dvbpsi_EmptyTDTTOT
 *****************************************************************************/
/*!
 * \fn void dvbpsi_EmptyTDTTOT(dvbpsi_tdt_tot_t* p_tdt_tot)
 * \brief Clean a dvbpsi_tdt_tot_t structure.
 * \param p_tdt_tot pointer to the TDT/TOT structure
 * \return nothing.
 */
void dvbpsi_EmptyTDTTOT(dvbpsi_tdt_tot_t *p_tdt_tot);

/*****************************************************************************
 * dvbpsi_TOTAddDescriptor
 *****************************************************************************/
/*!
 * \fn dvbpsi_descriptor_t* dvbpsi_TOTAddDescriptor(vbpsi_tdt_tot_t *p_tot,
                                                    uint8_t i_tag,
                                                    uint8_t i_length,
                                                    uint8_t* p_data)
 * \brief Add a descriptor in the TOT.
 * \param p_tot pointer to the TOT structure
 * \param i_tag descriptor's tag
 * \param i_length descriptor's length
 * \param p_data descriptor's data
 * \return a pointer to the added descriptor.
 */
dvbpsi_descriptor_t* dvbpsi_TOTAddDescriptor(dvbpsi_tdt_tot_t *p_tot,
                                             uint8_t i_tag, uint8_t i_length,
                                             uint8_t* p_data);

#endif
