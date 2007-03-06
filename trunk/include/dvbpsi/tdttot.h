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
    int                     i_year;  /*!< Year in UTC */
    int                     i_month; /*!< Month in UTC */
    int                     i_day;   /*!< Day in UTC */
    int                     i_hour;  /*!< Hour in UTC */
    int                     i_minute;/*!< Minute in UTC */
    int                     i_second;/*!< Second in UTC */
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
  p_tdt_tot = (dvbpsi_tdt_tot_t*)malloc(sizeof(dvbpsi_tdt_tot_t));  \
  if(p_tdt_tot != NULL)                                             \
    dvbpsi_InitTDTTOT(p_tdt_tot, i_year, i_month, i_day,            \
                      i_hour, i_minute, i_second);                  \
} while(0);


/*****************************************************************************
 * dvbpsi_EmptyTDTTOT/dvbpsi_DeleteTDTTOT
 *****************************************************************************/
/*!
 * \fn void dvbpsi_EmptyTDTTOT(dvbpsi_tdt_tot_t* p_tdt_tot)
 * \brief Clean a dvbpsi_tdt_tot_t structure.
 * \param p_tdt_tot pointer to the TDT/TOT structure
 * \return nothing.
 */
void dvbpsi_EmptyTDTTOT(dvbpsi_tdt_tot_t *p_tdt_tot);

/*!
 * \def dvbpsi_DeleteTDTTOT(p_nit)
 * \brief Clean and free a dvbpsi_nit_t structure.
 * \param p_nit pointer to the NIT structure
 * \return nothing.
 */
#define dvbpsi_DeleteTDTTOT(p_tdt_tot)                                  \
do {                                                                    \
  dvbpsi_EmptyTDTTOT(p_tdt_tot);                                        \
  free(p_tdt_tot);                                                      \
} while(0);

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

/*****************************************************************************
 * dvbpsi_DecodeMJDUTC
 *****************************************************************************/
/*!
 * \fn void dvbpsi_DecodeMJDUTC(char *p_mjdutc, 
                                int *p_year, 
                                int *p_month, 
                                int *p_day, 
                                int *p_hour, 
                                int *p_minute, 
                                int *p_second)
 * \brief Decode date/time encoded in MJD UTC format into its constituent parts.
 * \param p_mjdutc pointer to MJD UTC encoded date time.
 * \param p_year Integer to store the decoded year in.
 * \param p_month Integer to store the decoded month in.
 * \param p_day Integer to store the decoded day in.
 * \param p_hour Integer to store the decoded hour in.
 * \param p_minute Integer to store the decoded minute in.
 * \param p_second Integer to store the decoded second in.
 */
void dvbpsi_DecodeMJDUTC(char *p_mjdutc, int *p_year, int *p_month, int *p_day,
                                 int *p_hour, int *p_minute, int *p_second);
#endif
