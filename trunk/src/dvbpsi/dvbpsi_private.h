/*****************************************************************************
 * dvbpsi_private.h: main private header
 *----------------------------------------------------------------------------
 * (c)2001-2002 VideoLAN
 * $Id: dvbpsi_private.h 102 2004-12-22 12:09:54Z gbazin $
 *
 * Authors: Arnaud de Bossoreille de Ribou <bozo@via.ecp.fr>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 *----------------------------------------------------------------------------
 *
 *****************************************************************************/

#ifndef _DVBPSI_DVBPSI_PRIVATE_H_
#define _DVBPSI_DVBPSI_PRIVATE_H_

#include "logging.h"

/*****************************************************************************
 * Error management
 *****************************************************************************/
#define DVBPSI_ERROR(src, str)                                          \
        LogModule( LOG_DIARRHEA, "dvbpsi", "Error (" src "): " str "\n");
#ifdef HAVE_VARIADIC_MACROS
#  define DVBPSI_ERROR_ARG(src, str, x...)                              \
        LogModule( LOG_DIARRHEA, "dvbpsi",  "Error (" src "): " str "\n", x);
#else
   inline void DVBPSI_ERROR_ARG( char *src, const char *str, ... )
   {
        if (LogLevelIsEnabled(LOG_DIARRHEA))
        {
            va_list ap; char *line; 
            va_start( ap, str );
            asprintf(&line, str, ap); 
            LogModule( LOG_DIARRHEA, "dvbpsi", "Error (%s): %s\n", src, line); 
            free(line); 
            va_end( ap ); 
        }
    }
#endif

#ifdef DEBUG
#  define DVBPSI_DEBUG(src, str)                                        \
          LogModule( LOG_DIARRHEA,  "dvbpsi", "Debug (" src "): " str "\n");
#  ifdef HAVE_VARIADIC_MACROS
#     define DVBPSI_DEBUG_ARG(src, str, x...)                           \
          LogModule( LOG_DIARRHEA,  "dvbpsi", "Debug (" src "): " str "\n", x);
#  else
       inline void DVBPSI_DEBUG_ARG( char *src, const char *str, ... )
       {
            if (LogLevelIsEnabled(LOG_DIARRHEA))
            {
                va_list ap; char *line; 
                va_start( ap, str );
                asprintf(&line, str, ap); 
                LogModule( LOG_DIARRHEA, "dvbpsi", "Debug (%s): %s\n", src, line); 
                free(line); 
                va_end( ap ); 
            }
        }
#  endif
#else
#  define DVBPSI_DEBUG(src, str)
#  ifdef HAVE_VARIADIC_MACROS
#     define DVBPSI_DEBUG_ARG(src, str, x...)
#  else
      inline void DVBPSI_DEBUG_ARG( char *src, const char *str, ... ) {}
#  endif
#endif

#endif

