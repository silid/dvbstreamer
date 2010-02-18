/*
 * utils.h
 */

/*
 * Copyright (C) 2005, Simon Kilvington
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef __UTILS_H__
#define __UTILS_H__

#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>

#ifndef MAX
#define MAX(a, b)	((a) > (b) ? (a) : (b))
#endif

/* DVB demux device - %u is card number */
#if defined(HAVE_DREAMBOX_HARDWARE)
#define DEMUX_DEVICE "/dev/dvb/card%u/demux%u"
#else
#define DEMUX_DEVICE  "/dev/dvb/adapter%u/demux%u"
#endif

/* DVB dvr device - %u is card number */
#if defined(HAVE_DREAMBOX_HARDWARE)
#define DVR_DEVICE "/dev/dvb/card%u/dvr%u"
#else
#define DVR_DEVICE  "/dev/dvb/adapter%u/dvr%u"
#endif

/* DVB frontend device - %u is card number */
#if defined(HAVE_DREAMBOX_HARDWARE)
#define FE_DEVICE "/dev/dvb/card%u/frontend%u"
#else
#define FE_DEVICE  "/dev/dvb/adapter%u/frontend%u"
#endif

char *skip_ws(char *);

char hex_digit(uint8_t);

void *safe_malloc(size_t);
void *safe_realloc(void *, size_t);
void safe_free(void *);

void hexdump(unsigned char *, size_t);

void error(char *, ...);
void fatal(char *, ...);

/* in rb-download.c */
void verbose(char *, ...);
void vverbose(char *, ...);
void vhexdump(unsigned char *, size_t);

#endif	/* __UTILS_H__ */

