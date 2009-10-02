#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#if defined(HAVE_INTTYPES_H)
#include <inttypes.h>
#elif defined(HAVE_STDINT_H)
#include <stdint.h>
#endif

#include "dvbpsi.h"
#include "dvbpsi_private.h"
#include "psi.h"
#include "sections.h"

static void dvbpsi_SectionsCallback(dvbpsi_handle p_decoder, dvbpsi_psi_section_t * p_section);


/*****************************************************************************
 * dvbpsi_AttachDemux
 *****************************************************************************
 * Creation of the demux structure
 *****************************************************************************/
dvbpsi_handle dvbpsi_AttachSections(dvbpsi_sections_new_cb_t pf_new_cb,
                                   void *                p_new_cb_data)
{
  dvbpsi_handle h_dvbpsi = (dvbpsi_decoder_t*)malloc(sizeof(dvbpsi_decoder_t));
  dvbpsi_sections_t * p_sections;

  if(h_dvbpsi == NULL)
    return NULL;

  p_sections = (dvbpsi_sections_t*)malloc(sizeof(dvbpsi_sections_t));

  if(p_sections == NULL)
  {
    free(h_dvbpsi);
    return NULL;
  }

  /* PSI decoder configuration */
  h_dvbpsi->pf_callback = &dvbpsi_SectionsCallback;
  h_dvbpsi->p_private_decoder = p_sections;
  h_dvbpsi->i_section_max_size = 4096;
  /* PSI decoder initial state */
  h_dvbpsi->i_continuity_counter = 31;
  h_dvbpsi->b_discontinuity = 1;
  h_dvbpsi->p_current_section = NULL;
  h_dvbpsi->p_free_sections = NULL;

  /* Sutables demux configuration */
  p_sections->p_decoder = h_dvbpsi;
  p_sections->pf_new_callback = pf_new_cb;
  p_sections->p_new_cb_data = p_new_cb_data;

  return h_dvbpsi;
}

static void dvbpsi_SectionsCallback(dvbpsi_handle p_decoder, dvbpsi_psi_section_t * p_section)
{
  dvbpsi_sections_t * p_sections;

  p_sections = (dvbpsi_sections_t *)p_decoder->p_private_decoder;
  p_sections->pf_new_callback(p_sections->p_new_cb_data, p_decoder, p_section);
}

void dvbpsi_DetachSections(dvbpsi_handle h_dvbpsi)
{
  dvbpsi_sections_t * p_sections
                  = (dvbpsi_sections_t *)h_dvbpsi->p_private_decoder;

  free(p_sections);
  if(h_dvbpsi->p_current_section)
    dvbpsi_DeletePSISections(h_dvbpsi->p_current_section);

  if(h_dvbpsi->p_free_sections)
    dvbpsi_DeletePSISections(h_dvbpsi->p_free_sections);    

  free(h_dvbpsi);
}

void dvbpsi_PushSection(dvbpsi_handle h_dvbpsi, dvbpsi_psi_section_t * p_section)
{
    h_dvbpsi->pf_callback(h_dvbpsi, p_section);
}

