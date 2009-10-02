#ifndef _DVBPSI_SECTIONS_H_
#define _DVBPSI_SECTIONS_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*dvbpsi_sections_new_cb_t) (void *   p_cb_data,
                                       dvbpsi_handle h_dvbpsi,
                                       dvbpsi_psi_section_t* p_section);

/*****************************************************************************
 * dvbpsi_sections_t
 *****************************************************************************/
/*!
 * \struct dvbpsi_demux_s
 * \brief subtable demultiplexor structure
 *
 * This structure contains the subtables demultiplexor data, such as the
 * decoders and new subtable callback.
 */
/*!
 * \typedef struct dvbpsi_demux_s dvbpsi_demux_t
 * \brief dvbpsi_demux_t type definition.
 */
typedef struct dvbpsi_sections_s
{
  dvbpsi_handle             p_decoder;          /*!< Parent PSI Decoder */
  /* New subtable callback */
  dvbpsi_sections_new_cb_t  pf_new_callback;    /*!< New subtable callback */
  void *                    p_new_cb_data;      /*!< Data provided to the
                                                     previous callback */

} dvbpsi_sections_t;


dvbpsi_handle dvbpsi_AttachSections(dvbpsi_sections_new_cb_t pf_new_cb,
                                 void *                p_new_cb_data);


void dvbpsi_DetachSections(dvbpsi_handle h_dvbpsi);

void dvbpsi_PushSection(dvbpsi_handle h_dvbpsi, dvbpsi_psi_section_t* p_section);

#ifdef __cplusplus
};
#endif

#else
#error "Multiple inclusions of demux.h"
#endif


