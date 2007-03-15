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

tdttot.c

Decode Time Date Table and Time Offset Table.

*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <dvbpsi/dvbpsi.h>
#include <dvbpsi/psi.h>
#include <dvbpsi/descriptor.h>
#include "dvbpsi/tdttot.h"
#include "logging.h"
/*****************************************************************************
 * dvbpsi_tdt_tot_decoder_s
 *****************************************************************************
 * TDT/TOT decoder.
 *****************************************************************************/
typedef struct dvbpsi_tdt_tot_decoder_s
{
  dvbpsi_tdt_tot_callback       pf_callback;
  void *                        p_cb_data;
} dvbpsi_tdt_tot_decoder_t;

/*****************************************************************************
 * dvbpsi_GatherPATSections
 *****************************************************************************
 * Callback for the PSI decoder.
 *****************************************************************************/
void dvbpsi_GatherTDTTOTSections(dvbpsi_decoder_t* p_decoder,
                              dvbpsi_psi_section_t* p_section);
/*****************************************************************************
 * dvbpsi_DecodeTDTSection
 *****************************************************************************
 * TDT decoder.
 *****************************************************************************/
void dvbpsi_DecodeTDTSection(dvbpsi_tdt_tot_t* p_tdt,
                             dvbpsi_psi_section_t* p_section);
/*****************************************************************************
 * dvbpsi_DecodeTOTSection
 *****************************************************************************
 * TOT decoder.
 *****************************************************************************/
void dvbpsi_DecodeTOTSection(dvbpsi_tdt_tot_t* p_tot,
                             dvbpsi_psi_section_t* p_section);


/*****************************************************************************
 * dvbpsi_AttachTDTTOT
 *****************************************************************************
 * Initialize a TDT/TOT decoder and return a handle on it.
 *****************************************************************************/
dvbpsi_handle dvbpsi_AttachTDTTOT(dvbpsi_tdt_tot_callback pf_callback,
                                  void* p_cb_data)
{
  dvbpsi_handle h_dvbpsi = (dvbpsi_decoder_t*)malloc(sizeof(dvbpsi_decoder_t));
  dvbpsi_tdt_tot_decoder_t* p_tdt_tot_decoder;
  unsigned int i;

  if(h_dvbpsi == NULL)
    return NULL;

  p_tdt_tot_decoder = (dvbpsi_tdt_tot_decoder_t*)malloc(sizeof(dvbpsi_tdt_tot_decoder_t));

  if(p_tdt_tot_decoder == NULL)
  {
    free(h_dvbpsi);
    return NULL;
  }

  /* PSI decoder configuration */
  h_dvbpsi->pf_callback = &dvbpsi_GatherTDTTOTSections;
  h_dvbpsi->p_private_decoder = p_tdt_tot_decoder;
  h_dvbpsi->i_section_max_size = 1024;
  /* PSI decoder initial state */
  h_dvbpsi->i_continuity_counter = 31;
  h_dvbpsi->b_discontinuity = 1;
  h_dvbpsi->p_current_section = NULL;
  h_dvbpsi->p_free_sections = NULL;

  /* TDT/TOT decoder information */
  p_tdt_tot_decoder->pf_callback = pf_callback;
  p_tdt_tot_decoder->p_cb_data = p_cb_data;

  return h_dvbpsi;
}


/*****************************************************************************
 * dvbpsi_DetachTDTTOT
 *****************************************************************************
 * Close a TDT/TOT decoder. The handle isn't valid any more.
 *****************************************************************************/
void dvbpsi_DetachTDTTOT(dvbpsi_handle h_dvbpsi)
{
  free(h_dvbpsi->p_private_decoder);
  if(h_dvbpsi->p_current_section)
    dvbpsi_DeletePSISections(h_dvbpsi->p_current_section);
  if(h_dvbpsi->p_free_sections)
    dvbpsi_DeletePSISections(h_dvbpsi->p_free_sections);
  free(h_dvbpsi);
}


/*****************************************************************************
 * dvbpsi_InitTDTTOT
 *****************************************************************************
 * Initialize a pre-allocated dvbpsi_tdt_tot_t structure.
 *****************************************************************************/
void dvbpsi_InitTDTTOT(dvbpsi_tdt_tot_t *p_tdt_tot,
                        int i_year, int i_month, int i_day,
                        int i_hour, int i_minute, int i_second)
{
  p_tdt_tot->i_year = i_year;
  p_tdt_tot->i_month = i_month;
  p_tdt_tot->i_day = i_day;
  p_tdt_tot->i_hour = i_hour;
  p_tdt_tot->i_minute = i_minute;
  p_tdt_tot->i_second = i_second;
  p_tdt_tot->p_first_descriptor = NULL;
}

/*****************************************************************************
 * dvbpsi_EmptyTDTTOT
 *****************************************************************************
 * Clean a dvbpsi_tdt_tot_t structure.
 *****************************************************************************/
void dvbpsi_EmptyTDTTOT(dvbpsi_tdt_tot_t *p_tdt_tot)
{
    dvbpsi_DeleteDescriptors(p_tdt_tot->p_first_descriptor);
}

/*****************************************************************************
 * dvbpsi_GatherPATSections
 *****************************************************************************
 * Callback for the PSI decoder.
 *****************************************************************************/
void dvbpsi_GatherTDTTOTSections(dvbpsi_decoder_t* p_decoder,
                              dvbpsi_psi_section_t* p_section)
{
  dvbpsi_tdt_tot_decoder_t* p_tdt_tot_decoder
                        = (dvbpsi_tdt_tot_decoder_t*)p_decoder->p_private_decoder;

  if(p_section->i_table_id == 0x70)
  {
    dvbpsi_tdt_tot_t* p_tdt;
    dvbpsi_NewTDTTOT(p_tdt, 0,0,0,0,0,0);
    dvbpsi_DecodeTDTSection(p_tdt, p_section);
    p_tdt_tot_decoder->pf_callback(p_tdt_tot_decoder->p_cb_data, p_tdt);
  }

  if(p_section->i_table_id == 0x73)
  {
    dvbpsi_tdt_tot_t* p_tot;
    dvbpsi_NewTDTTOT(p_tot, 0,0,0,0,0,0);
    dvbpsi_DecodeTOTSection(p_tot, p_section);
    p_tdt_tot_decoder->pf_callback(p_tdt_tot_decoder->p_cb_data, p_tot);
  }

  dvbpsi_ReleasePSISections(p_decoder, p_section);
}


/*****************************************************************************
 * dvbpsi_DecodeTDTSection
 *****************************************************************************
 * TDT decoder.
 *****************************************************************************/
void dvbpsi_DecodeTDTSection(dvbpsi_tdt_tot_t* p_tdt,
                             dvbpsi_psi_section_t* p_section)
{
    // Decode UTC Time
    dvbpsi_DecodeMJDUTC(p_section->p_payload_start, &p_tdt->i_year, &p_tdt->i_month, &p_tdt->i_day, 
        &p_tdt->i_hour, &p_tdt->i_minute, &p_tdt->i_second);
    p_tdt->p_first_descriptor = NULL;
}

/*****************************************************************************
 * dvbpsi_DecodeTOTSection
 *****************************************************************************
 * TOT decoder.
 *****************************************************************************/
void dvbpsi_DecodeTOTSection(dvbpsi_tdt_tot_t* p_tot,
                             dvbpsi_psi_section_t* p_section)
{
    uint16_t i_length;
    uint8_t *p_byte, *p_end;

    // Decode UTC Time
    p_byte = p_section->p_payload_start;
    dvbpsi_DecodeMJDUTC(p_byte, &p_tot->i_year, &p_tot->i_month, &p_tot->i_day, 
        &p_tot->i_hour, &p_tot->i_minute, &p_tot->i_second);
    p_byte += 5;
    i_length = ((p_byte[0] & 0xf) << 8) | p_byte[1];
    p_byte += 2;
    p_end = p_byte + i_length;

    if( p_end > p_section->p_payload_end ) return;

    while(p_byte + 2 <= p_end)
    {
        uint8_t i_tag = p_byte[0];
        uint8_t i_desc_length = p_byte[1];
        if(i_desc_length + 2 <= p_end - p_byte)
          dvbpsi_TOTAddDescriptor(p_tot, i_tag, i_desc_length, p_byte + 2);
        p_byte += 2 + i_desc_length;
    }
}


void dvbpsi_DecodeMJDUTC(char *p_mjdutc, int *p_year, int *p_month, int *p_day, int *p_hour, int *p_minute, int *p_second)
{
    #define BCD_CHAR_TO_INT(_bcd) (((_bcd >> 4) * 10) + (_bcd & 0x0f))

    uint16_t i_mjd = (((uint16_t)p_mjdutc[0] << 8) | (uint16_t)(p_mjdutc[1] & 0xff));
    double d_mjd = (double)i_mjd;
    /*
        To find Y, M, D from MJD
        Y' = int [ (MJD - 15 078,2) / 365,25 ]
        M' = int { [ MJD - 14 956,1 - int (Y' × 365,25) ] / 30,6001 }
        D = MJD - 14 956 - int (Y' × 365,25) - int (M' × 30,6001)
        If M' = 14 or M' = 15, then K = 1; else K = 0
        Y = Y' + K
        M = M' - 1 - K × 12
    */
    int i_temp_y, i_temp_m, i_k = 0;
    i_temp_y = (int) ((d_mjd - 15078.2)/ 365.25);
    i_temp_m = (int) (((d_mjd - 14956.1) - ((double)i_temp_y * 365.25)) / 30.6001);

    if ((i_temp_m == 14) || (i_temp_m == 15))
    {
        i_k = 1;
    }

    *p_year = i_temp_y + i_k + 1900;
    *p_month = (i_temp_m - 1) - (i_k * 12);
    *p_day = (((i_mjd - 14956) - (int)((double)i_temp_y * 365.25)) - (int)((double)i_temp_m * 30.6001));
    *p_hour = BCD_CHAR_TO_INT(p_mjdutc[2]);
    *p_minute = BCD_CHAR_TO_INT(p_mjdutc[3]);
    *p_second = BCD_CHAR_TO_INT(p_mjdutc[4]);
}

/*****************************************************************************
 * dvbpsi_TOTAddDescriptor
 *****************************************************************************
 * Add a descriptor in the TOT description.
 *****************************************************************************/
dvbpsi_descriptor_t *dvbpsi_TOTAddDescriptor(
                                               dvbpsi_tdt_tot_t *p_tot,
                                               uint8_t i_tag, uint8_t i_length,
                                               uint8_t *p_data)
{
  dvbpsi_descriptor_t * p_descriptor
                        = dvbpsi_NewDescriptor(i_tag, i_length, p_data);

  if(p_descriptor)
  {
    if(p_tot->p_first_descriptor == NULL)
    {
      p_tot->p_first_descriptor = p_descriptor;
    }
    else
    {
      dvbpsi_descriptor_t * p_last_descriptor = p_tot->p_first_descriptor;
      while(p_last_descriptor->p_next != NULL)
        p_last_descriptor = p_last_descriptor->p_next;
      p_last_descriptor->p_next = p_descriptor;
    }
  }

  return p_descriptor;
}
