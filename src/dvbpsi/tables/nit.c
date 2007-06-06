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

nit.c

Decode Network Information Tables.

*/
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "dvbpsi.h"
#include "psi.h"
#include "descriptor.h"
#include "demux.h"
#include "dvbpsi/nit.h"
#include "dvbpsi_private.h"

/*****************************************************************************
 * dvbpsi_nit_decoder_t
 *****************************************************************************
 * NIT decoder.
 *****************************************************************************/
typedef struct dvbpsi_nit_decoder_s
{
  dvbpsi_nit_callback           pf_callback;
  void *                        p_cb_data;

  dvbpsi_nit_t                  current_nit;
  dvbpsi_nit_t *                p_building_nit;

  int                           b_current_valid;

  uint8_t                       i_last_section_number;
  dvbpsi_psi_section_t *        ap_sections [256];

} dvbpsi_nit_decoder_t;


/*****************************************************************************
 * dvbpsi_GatherNITSections
 *****************************************************************************
 * Callback for the PSI decoder.
 *****************************************************************************/
void dvbpsi_GatherNITSections(dvbpsi_decoder_t* p_psi_decoder,
		              void* p_private_decoder,
                              dvbpsi_psi_section_t* p_section);


/*****************************************************************************
 * dvbpsi_DecodeNITSection
 *****************************************************************************
 * NIT decoder.
 *****************************************************************************/
void dvbpsi_DecodeNITSections(dvbpsi_nit_t* p_nit,
                              dvbpsi_psi_section_t* p_section);

dvbpsi_descriptor_t *dvbpsi_NITAddDescriptor(
                                               dvbpsi_nit_t *p_nit,
                                               uint8_t i_tag, uint8_t i_length,
                                               uint8_t *p_data);
dvbpsi_nit_transport_t *dvbpsi_NITAddTransport(dvbpsi_nit_t* p_nit,
                                           uint16_t i_ts_id,
                                           uint16_t i_orignal_network_id);
dvbpsi_descriptor_t *dvbpsi_NITTransportAddDescriptor(
                                               dvbpsi_nit_transport_t *p_transport,
                                               uint8_t i_tag, uint8_t i_length,
                                               uint8_t *p_data);
/*****************************************************************************
 * dvbpsi_AttachNIT
 *****************************************************************************
 * Initialize a NIT subtable decoder.
 *****************************************************************************/
int dvbpsi_AttachNIT(dvbpsi_decoder_t * p_psi_decoder, uint8_t i_table_id,
          uint16_t i_extension, dvbpsi_nit_callback pf_callback,
                               void* p_cb_data)
{
  dvbpsi_demux_t* p_demux = (dvbpsi_demux_t*)p_psi_decoder->p_private_decoder;
  dvbpsi_demux_subdec_t* p_subdec;
  dvbpsi_nit_decoder_t*  p_nit_decoder;
  unsigned int i;

  if(dvbpsi_demuxGetSubDec(p_demux, i_table_id, i_extension))
  {
    DVBPSI_ERROR_ARG("NIT decoder",
                     "Already a decoder for (table_id == 0x%02x,"
                     "extension == 0x%02x)",
                     i_table_id, i_extension);

    return 1;
  }

  p_subdec = (dvbpsi_demux_subdec_t*)malloc(sizeof(dvbpsi_demux_subdec_t));
  if(p_subdec == NULL)
  {
    return 1;
  }

  p_nit_decoder = (dvbpsi_nit_decoder_t*)malloc(sizeof(dvbpsi_nit_decoder_t));

  if(p_nit_decoder == NULL)
  {
    free(p_subdec);
    return 1;
  }

  /* subtable decoder configuration */
  p_subdec->pf_callback = &dvbpsi_GatherNITSections;
  p_subdec->p_cb_data = p_nit_decoder;
  p_subdec->i_id = (uint32_t)i_table_id << 16 | (uint32_t)i_extension;
  p_subdec->pf_detach = dvbpsi_DetachNIT;

  /* Attach the subtable decoder to the demux */
  p_subdec->p_next = p_demux->p_first_subdec;
  p_demux->p_first_subdec = p_subdec;

  /* NIT decoder information */
  p_nit_decoder->pf_callback = pf_callback;
  p_nit_decoder->p_cb_data = p_cb_data;
  /* NIT decoder initial state */
  p_nit_decoder->b_current_valid = 0;
  p_nit_decoder->p_building_nit = NULL;
  for(i = 0; i <= 255; i++)
    p_nit_decoder->ap_sections[i] = NULL;

  return 0;
}


/*****************************************************************************
 * dvbpsi_DetachNIT
 *****************************************************************************
 * Close a NIT decoder.
 *****************************************************************************/
void dvbpsi_DetachNIT(dvbpsi_demux_t * p_demux, uint8_t i_table_id,
          uint16_t i_extension)
{
  dvbpsi_demux_subdec_t* p_subdec;
  dvbpsi_demux_subdec_t** pp_prev_subdec;
  dvbpsi_nit_decoder_t* p_nit_decoder;

  unsigned int i;

  p_subdec = dvbpsi_demuxGetSubDec(p_demux, i_table_id, i_extension);

  if(p_demux == NULL)
  {
    DVBPSI_ERROR_ARG("NIT decoder",
                     "No such NIT decoder (table_id == 0x%02x,"
                     "extension == 0x%02x)",
                     i_table_id, i_extension);
    return;
  }

  p_nit_decoder = (dvbpsi_nit_decoder_t*)p_subdec->p_cb_data;

  free(p_nit_decoder->p_building_nit);

  for(i = 0; i <= 255; i++)
  {
    if(p_nit_decoder->ap_sections[i])
      dvbpsi_DeletePSISections(p_nit_decoder->ap_sections[i]);
  }

  free(p_subdec->p_cb_data);

  pp_prev_subdec = &p_demux->p_first_subdec;
  while(*pp_prev_subdec != p_subdec)
    pp_prev_subdec = &(*pp_prev_subdec)->p_next;

  *pp_prev_subdec = p_subdec->p_next;
  free(p_subdec);
}


/*****************************************************************************
 * dvbpsi_InitNIT
 *****************************************************************************
 * Initialize a pre-allocated dvbpsi_nit_t structure.
 *****************************************************************************/
void dvbpsi_InitNIT(dvbpsi_nit_t* p_nit, uint16_t i_network_id, uint8_t i_version,
                    int b_current_next)
{
  p_nit->i_network_id = i_network_id;
  p_nit->i_version = i_version;
  p_nit->b_current_next = b_current_next;
  p_nit->p_first_descriptor = NULL;
  p_nit->p_first_transport = NULL;
}


/*****************************************************************************
 * dvbpsi_EmptyNIT
 *****************************************************************************
 * Clean a dvbpsi_nit_t structure.
 *****************************************************************************/
void dvbpsi_EmptyNIT(dvbpsi_nit_t* p_nit)
{
  dvbpsi_nit_transport_t *p_transport = p_nit->p_first_transport;
  dvbpsi_DeleteDescriptors(p_nit->p_first_descriptor);
  while (p_transport)
  {
    dvbpsi_nit_transport_t *p_next;
    dvbpsi_DeleteDescriptors(p_transport->p_first_descriptor);
    p_next = p_transport->p_next;
    free(p_transport);
    p_transport = p_next;
  }
}



/*****************************************************************************
 * dvbpsi_GatherNITSections
 *****************************************************************************
 * Callback for the subtable demultiplexor.
 *****************************************************************************/
void dvbpsi_GatherNITSections(dvbpsi_decoder_t * p_psi_decoder,
                              void * p_private_decoder,
                              dvbpsi_psi_section_t * p_section)
{
  dvbpsi_nit_decoder_t * p_nit_decoder
                        = (dvbpsi_nit_decoder_t*)p_private_decoder;
  int b_append = 1;
  int b_reinit = 0;
  unsigned int i;
/*
  printlog(LOG_ERROR, "NIT decoder: "
                   "Table version %2d, " "i_table_id %2d, " "i_extension %5d, "
                   "section %3d up to %3d, " "current %1d",
                   p_section->i_version, p_section->i_table_id,
                   p_section->i_extension,
                   p_section->i_number, p_section->i_last_number,
                   p_section->b_current_next);
*/
  if(!p_section->b_syntax_indicator)
  {
    /* Invalid section_syntax_indicator */
    DVBPSI_ERROR("NIT decoder",
                 "invalid section (section_syntax_indicator == 0)");
    b_append = 0;
  }

  /* Now if b_append is true then we have a valid NIT section */
  if(b_append)
  {
    /* TS discontinuity check */
    if(p_psi_decoder->b_discontinuity)
    {
      b_reinit = 1;
      p_psi_decoder->b_discontinuity = 0;
    }
    else
    {
      /* Perform a few sanity checks */
      if(p_nit_decoder->p_building_nit)
      {
        if(p_nit_decoder->p_building_nit->i_network_id != p_section->i_extension)
        {
          /* transport_stream_id */
          DVBPSI_ERROR("NIT decoder",
                       "'transport_stream_id' differs"
                       " whereas no TS discontinuity has occured");
          b_reinit = 1;
        }
        else if(p_nit_decoder->p_building_nit->i_version
                                                != p_section->i_version)
        {
          /* version_number */
          DVBPSI_ERROR("NIT decoder",
                       "'version_number' differs"
                       " whereas no discontinuity has occured");
          b_reinit = 1;
        }
        else if(p_nit_decoder->i_last_section_number !=
                                                p_section->i_last_number)
        {
          /* last_section_number */
          DVBPSI_ERROR("NIT decoder",
                       "'last_section_number' differs"
                       " whereas no discontinuity has occured");
          b_reinit = 1;
        }
      }
      else
      {
        if(    (p_nit_decoder->b_current_valid)
            && (p_nit_decoder->current_nit.i_version == p_section->i_version))
        {
          /* Signal a new NIT if the previous one wasn't active */
          if(    (!p_nit_decoder->current_nit.b_current_next)
              && (p_section->b_current_next))
          {
            dvbpsi_nit_t * p_nit = (dvbpsi_nit_t*)malloc(sizeof(dvbpsi_nit_t));

            p_nit_decoder->current_nit.b_current_next = 1;
            *p_nit = p_nit_decoder->current_nit;
            p_nit_decoder->pf_callback(p_nit_decoder->p_cb_data, p_nit);
          }

          /* Don't decode since this version is already decoded */
          b_append = 0;
        }
      }
    }
  }

  /* Reinit the decoder if wanted */
  if(b_reinit)
  {
    /* Force redecoding */
    p_nit_decoder->b_current_valid = 0;
    /* Free structures */
    if(p_nit_decoder->p_building_nit)
    {
      free(p_nit_decoder->p_building_nit);
      p_nit_decoder->p_building_nit = NULL;
    }
    /* Clear the section array */
    for(i = 0; i <= 255; i++)
    {
      if(p_nit_decoder->ap_sections[i] != NULL)
      {
        dvbpsi_ReleasePSISections(p_psi_decoder, p_nit_decoder->ap_sections[i]);
        p_nit_decoder->ap_sections[i] = NULL;
      }
    }
  }

  /* Append the section to the list if wanted */
  if(b_append)
  {
    int b_complete;

    /* Initialize the structures if it's the first section received */
    if(!p_nit_decoder->p_building_nit)
    {
      p_nit_decoder->p_building_nit =
                                (dvbpsi_nit_t*)malloc(sizeof(dvbpsi_nit_t));
      dvbpsi_InitNIT(p_nit_decoder->p_building_nit,
                     p_section->i_extension,
                     p_section->i_version,
                     p_section->b_current_next);
      p_nit_decoder->i_last_section_number = p_section->i_last_number;
    }

    /* Fill the section array */
    if(p_nit_decoder->ap_sections[p_section->i_number] != NULL)
    {
      DVBPSI_ERROR_ARG("NIT decoder", "NIT decoder: overwrite section number %d",
                       p_section->i_number);
      dvbpsi_ReleasePSISections(p_psi_decoder, p_nit_decoder->ap_sections[p_section->i_number]);
    }
    p_nit_decoder->ap_sections[p_section->i_number] = p_section;

    /* Check if we have all the sections */
    b_complete = 0;
    for(i = 0; i <= p_nit_decoder->i_last_section_number; i++)
    {
      if(!p_nit_decoder->ap_sections[i])
        break;

      if(i == p_nit_decoder->i_last_section_number)
        b_complete = 1;
    }

    if(b_complete)
    {
      /* Save the current information */
      p_nit_decoder->current_nit = *p_nit_decoder->p_building_nit;
      p_nit_decoder->b_current_valid = 1;
      /* Chain the sections */
      if(p_nit_decoder->i_last_section_number)
      {
        for(i = 0; i <= p_nit_decoder->i_last_section_number - 1; i++)
          p_nit_decoder->ap_sections[i]->p_next =
                                        p_nit_decoder->ap_sections[i + 1];
      }
      /* Decode the sections */
      dvbpsi_DecodeNITSections(p_nit_decoder->p_building_nit,
                               p_nit_decoder->ap_sections[0]);
      /* Delete the sections */
      dvbpsi_ReleasePSISections(p_psi_decoder, p_nit_decoder->ap_sections[0]);
      /* signal the new NIT */
      p_nit_decoder->pf_callback(p_nit_decoder->p_cb_data,
                                 p_nit_decoder->p_building_nit);
      /* Reinitialize the structures */
      p_nit_decoder->p_building_nit = NULL;
      for(i = 0; i <= p_nit_decoder->i_last_section_number; i++)
        p_nit_decoder->ap_sections[i] = NULL;
    }
  }
  else
  {
    dvbpsi_ReleasePSISections(p_psi_decoder, p_section);
  }
}


/*****************************************************************************
 * dvbpsi_DecodeNITSection
 *****************************************************************************
 * NIT decoder.
 *****************************************************************************/
void dvbpsi_DecodeNITSections(dvbpsi_nit_t* p_nit,
                              dvbpsi_psi_section_t* p_section)
{
  uint8_t *p_byte, *p_end;
  uint8_t *p_ts_end;
  uint16_t i_ts_length;

  while(p_section)
  {
    for(p_byte = p_section->p_payload_start;
        p_byte + 4 < p_section->p_payload_end;)
    {
      uint16_t i_length = ((uint16_t)(p_byte[0] & 0xf) <<8) | p_byte[1];
      /* network descriptors */
      p_byte += 2;
      p_end = p_byte + i_length;
      if( p_end > p_section->p_payload_end ) break;

      while(p_byte + 2 <= p_end)
      {
        uint8_t i_tag = p_byte[0];
        uint8_t i_desc_length = p_byte[1];
        if(i_desc_length + 2 <= p_end - p_byte)
          dvbpsi_NITAddDescriptor(p_nit, i_tag, i_desc_length, p_byte + 2);
        p_byte += 2 + i_desc_length;
      }

      i_ts_length = ((uint16_t)(p_byte[0] & 0xf) <<8) | p_byte[1];
      p_byte += 2;
      p_ts_end = p_byte + i_ts_length;
      if( p_ts_end > p_section->p_payload_end ) break;

      while(p_byte + 2 <= p_ts_end)
      {
          uint16_t i_ts_id = ((uint16_t)p_byte[0]<<8) | p_byte[1];
          uint16_t i_original_network_id = ((uint16_t)p_byte[2]<<8) | p_byte[3];
          dvbpsi_nit_transport_t *p_transport = dvbpsi_NITAddTransport(p_nit, i_ts_id, i_original_network_id);
          i_length = ((uint16_t)(p_byte[4] & 0xf) <<8) | p_byte[5];
          p_byte += 6;

          p_end = p_byte + i_length;
          if( p_end > p_section->p_payload_end ) break;

          while(p_byte + 2 <= p_end)
          {
            uint8_t i_tag = p_byte[0];
            uint8_t i_desc_length = p_byte[1];
            if(i_desc_length + 2 <= p_end - p_byte)
              dvbpsi_NITTransportAddDescriptor(p_transport, i_tag, i_desc_length, p_byte + 2);
            p_byte += 2 + i_desc_length;
          }
      }
    }

    p_section = p_section->p_next;
  }
}



/*****************************************************************************
 * dvbpsi_NITAddDescriptor
 *****************************************************************************
 * Add a descriptor in the NIT description.
 *****************************************************************************/
dvbpsi_descriptor_t *dvbpsi_NITAddDescriptor(
                                               dvbpsi_nit_t *p_nit,
                                               uint8_t i_tag, uint8_t i_length,
                                               uint8_t *p_data)
{
  dvbpsi_descriptor_t * p_descriptor
                        = dvbpsi_NewDescriptor(i_tag, i_length, p_data);

  if(p_descriptor)
  {
    if(p_nit->p_first_descriptor == NULL)
    {
      p_nit->p_first_descriptor = p_descriptor;
    }
    else
    {
      dvbpsi_descriptor_t * p_last_descriptor = p_nit->p_first_descriptor;
      while(p_last_descriptor->p_next != NULL)
        p_last_descriptor = p_last_descriptor->p_next;
      p_last_descriptor->p_next = p_descriptor;
    }
  }

  return p_descriptor;
}


/*****************************************************************************
 * dvbpsi_NITAddTransport
 *****************************************************************************
 * Add a transport description at the end of the NIT.
 *****************************************************************************/
dvbpsi_nit_transport_t *dvbpsi_NITAddTransport(dvbpsi_nit_t* p_nit,
                                           uint16_t i_ts_id,
                                           uint16_t i_original_network_id)
{
  dvbpsi_nit_transport_t * p_transport
                = (dvbpsi_nit_transport_t*)malloc(sizeof(dvbpsi_nit_transport_t));

  if(p_transport)
  {
    p_transport->i_ts_id = i_ts_id;
    p_transport->i_original_network_id = i_original_network_id;
    p_transport->p_first_descriptor = NULL;
    p_transport->p_next = NULL;
    if(p_nit->p_first_transport == NULL)
    {
      p_nit->p_first_transport = p_transport;
    }
    else
    {
      dvbpsi_nit_transport_t * p_last_transport = p_nit->p_first_transport;
      while(p_last_transport->p_next != NULL)
        p_last_transport = p_last_transport->p_next;
      p_last_transport->p_next = p_transport;
    }
  }

  return p_transport;
}


/*****************************************************************************
 * dvbpsi_NITTransportAddDescriptor
 *****************************************************************************
 * Add a descriptor in the NIT transport description.
 *****************************************************************************/
dvbpsi_descriptor_t *dvbpsi_NITTransportAddDescriptor(
                                               dvbpsi_nit_transport_t *p_transport,
                                               uint8_t i_tag, uint8_t i_length,
                                               uint8_t *p_data)
{
  dvbpsi_descriptor_t * p_descriptor
                        = dvbpsi_NewDescriptor(i_tag, i_length, p_data);

  if(p_descriptor)
  {
    if(p_transport->p_first_descriptor == NULL)
    {
      p_transport->p_first_descriptor = p_descriptor;
    }
    else
    {
      dvbpsi_descriptor_t * p_last_descriptor = p_transport->p_first_descriptor;
      while(p_last_descriptor->p_next != NULL)
        p_last_descriptor = p_last_descriptor->p_next;
      p_last_descriptor->p_next = p_descriptor;
    }
  }

  return p_descriptor;
}
