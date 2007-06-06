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

atsctext.c

Convert ATSC Multiple strings text to UTF-8.

Note: Huffman decode and tables are taken from pchdtvr.c by inkling@nop.org
The tables are copyrighted by (from A/65C Annex C Page 91 footnote 19):
*********************************************************************
* Tables C4 through C7 are (C) 1997 General Instrument Corporation. *
* Unlimited use in conjunction with this ATSC standard is granted   *
* on a royalty-free basis by General Instrument Corporation.        *
*********************************************************************

*/
#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <iconv.h>

#include "logging.h"
#include "objects.h"
#include "atsctext.h"

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void ATSCMultipleStringsDestructor(ATSCMultipleStrings_t *strings);
static uint8_t *AppendSegment(uint8_t *segment, int *sbIndex, bool *supported);
static void HuffmanDecode(uint8_t *dest, uint8_t *src, int destLen, int srcLen, int comp);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
/********************************************************* ATSC HUFFMAN DECODE
 A/65b Table C5 Huffman Title Decode Tree (c) 1997 General Instruments Corp.

 not exact table, extracted from specs by me, faster and more useful this way
 since exact table is big endian but x86 isn't.

 byte offsets of character i tree root
*/

static unsigned int huffman1bo[128] = { 
0x0000, 0x003A, 0x003C, 0x003E, 0x0040, 0x0042, 0x0044, 0x0046, 
0x0048, 0x004A, 0x004C, 0x004E, 0x0050, 0x0052, 0x0054, 0x0056, 
0x0058, 0x005A, 0x005C, 0x005E, 0x0060, 0x0062, 0x0064, 0x0066, 
0x0068, 0x006A, 0x006C, 0x006E, 0x0070, 0x0072, 0x0074, 0x0076, 
0x0078, 0x00CE, 0x00D2, 0x00D4, 0x00D6, 0x00D8, 0x00DA, 0x00DC, 
0x00E6, 0x00E8, 0x00EA, 0x00F0, 0x00F2, 0x00F4, 0x0106, 0x0112, 
0x0114, 0x011C, 0x0128, 0x0130, 0x0134, 0x0136, 0x0138, 0x013A, 
0x013C, 0x013E, 0x0146, 0x0148, 0x014A, 0x014C, 0x014E, 0x0150, 
0x0152, 0x0154, 0x017E, 0x0192, 0x01AC, 0x01BA, 0x01D2, 0x01E4, 
0x01FA, 0x0206, 0x021E, 0x0226, 0x0232, 0x023E, 0x0252, 0x0264, 
0x027A, 0x0294, 0x0298, 0x02A4, 0x02C8, 0x02DE, 0x02E6, 0x02F4, 
0x0304, 0x0306, 0x030C, 0x0310, 0x0312, 0x0314, 0x0316, 0x0318, 
0x031A, 0x031C, 0x0352, 0x036A, 0x038E, 0x03AE, 0x03EE, 0x0406, 
0x0428, 0x0444, 0x0472, 0x0476, 0x0490, 0x04BE, 0x04D6, 0x050A, 
0x0544, 0x0564, 0x0566, 0x059A, 0x05D0, 0x05FC, 0x0622, 0x062C, 
0x0646, 0x0654, 0x067C, 0x068A, 0x068C, 0x068E, 0x0690, 0x0692
};

/*
 character i order-1 trees
 NOTE: byte lookup should not have endian issues
 also extracted from specs by me
*/

#define TITLE_COZ 1683
static unsigned char huffman1co[1684] = { 
0x1B,0x1C,0xB4,0xA4,0xB2,0xB7,0xDA,0x01,0xD1,0x02,0x03,0x9B,0x04,0xD5,0xD9,0x05,
0xCB,0xD6,0x06,0xCF,0x07,0x08,0xCA,0x09,0xC9,0xC5,0xC6,0x0A,0xD2,0xC4,0xC7,0xCC,
0xD0,0xC8,0xD7,0xCE,0x0B,0xC1,0x0C,0xC2,0xCD,0xC3,0x0D,0x0E,0x0F,0x10,0xD3,0x11,
0xD4,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1A,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,
0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,
0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,
0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,
0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x29,0x2A,0xD8,0xE5,0xB9,0x01,0xA7,0xB1,
0xEC,0xD1,0x02,0xAD,0xB2,0xDA,0xE3,0xB3,0x03,0xE4,0xE6,0x04,0x9B,0xE2,0x05,0x06,
0x07,0x08,0x09,0xD5,0x0A,0xD6,0x0B,0xD9,0x0C,0xA6,0xE9,0xCB,0xC5,0xCF,0x0D,0x0E,
0xCA,0xC9,0x0F,0xC7,0x10,0x11,0xE1,0x12,0x13,0xC6,0xD2,0xC8,0xCE,0xC1,0xC4,0xD0,
0xCC,0x14,0x15,0xEF,0xC2,0xD7,0x16,0xCD,0x17,0xF4,0xD4,0x18,0x19,0x1A,0xC3,0xD3,
0x1B,0x1C,0x1D,0x1E,0x1F,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x01,0x80,
0xA0,0x9B,0x9B,0x9B,0x9B,0x9B,0xB1,0x9B,0x9B,0x9B,0x9B,0xA0,0x04,0xF3,0xE4,0xB9,
0x01,0xF4,0xA0,0x9B,0x02,0x03,0x9B,0x9B,0x9B,0x9B,0x01,0x02,0x9B,0xC1,0xC8,0xD3,
0x9B,0x9B,0x9B,0xA0,0x07,0x08,0xB1,0xD2,0xD3,0xD4,0xD5,0xAD,0xCD,0xC1,0x01,0x02,
0x03,0xA0,0x04,0x9B,0x05,0x06,0xA0,0x05,0xC9,0xD7,0xD3,0x01,0x02,0x9B,0xAE,0x80,
0x03,0x04,0x9B,0x9B,0x02,0x03,0xAD,0x9B,0x01,0x80,0xA0,0xB0,0x04,0x05,0x80,0x9B,
0xB1,0xB2,0xA0,0xB0,0xB9,0x01,0x02,0x03,0x02,0x03,0xB1,0xBA,0x01,0xB0,0x9B,0x80,
0x80,0x01,0xB0,0x9B,0x9B,0xB8,0x9B,0x9B,0x9B,0x9B,0x9B,0xB0,0x9B,0xA0,0x02,0x03,
0xB1,0xB3,0xB9,0xB0,0x01,0x9B,0x9B,0xA0,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,
0x9B,0x80,0x9B,0x9B,0x13,0x14,0xAA,0xAD,0xAE,0xF6,0xE7,0xF4,0xE2,0xE9,0x01,0x02,
0xC2,0xF0,0x9B,0xF3,0xE3,0xE6,0xF7,0x03,0xF5,0x04,0x05,0x06,0xF2,0x07,0x08,0x09,
0x0A,0x0B,0x0C,0xE4,0xA0,0x0D,0xEC,0xEE,0x0E,0xED,0x0F,0x10,0x11,0x12,0x08,0x09,
0xC1,0xD3,0x9B,0x01,0xC3,0x02,0xE9,0xEC,0x03,0xF2,0xF5,0x04,0xEF,0xE1,0x05,0xE5,
0x06,0x07,0x0B,0x0C,0xC1,0xF9,0x01,0xC2,0xCF,0xE5,0xF5,0x9B,0xE9,0x02,0xA0,0x03,
0x04,0x05,0xF2,0x06,0xEC,0x07,0xE1,0x08,0x09,0xE8,0x0A,0xEF,0x05,0x06,0xF9,0x9B,
0x01,0xF5,0x02,0xF2,0xE9,0xE5,0xEF,0x03,0xE1,0x04,0x0A,0x0B,0xF1,0xF5,0xF3,0x01,
0xED,0xF9,0xC3,0x02,0xEC,0xEE,0xE4,0xF8,0x03,0x9B,0xF6,0x04,0x05,0xE1,0x06,0x07,
0x08,0x09,0x07,0x08,0xA0,0x9B,0xCC,0x01,0xE5,0x02,0xEC,0xF5,0xEF,0x03,0xE9,0xF2,
0x04,0x05,0xE1,0x06,0x09,0x0A,0xAE,0xEC,0xF9,0xC1,0xE8,0x01,0x9B,0x02,0x03,0x04,
0xE1,0xF5,0xE9,0x05,0xE5,0x06,0xF2,0xEF,0x07,0x08,0xEF,0x05,0x80,0x9B,0xF5,0x01,
0x02,0xE9,0xE1,0x03,0xE5,0x04,0xEE,0x0B,0xBA,0xD4,0xAE,0xF2,0xE3,0x01,0xA0,0x02,
0x80,0x9B,0xED,0x03,0xC9,0xF3,0xF4,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x02,0x03,
0x9B,0xF5,0x01,0xE1,0xEF,0xE5,0x05,0xE9,0xE1,0xEF,0xF5,0xEE,0x9B,0xE5,0x01,0x02,
0x03,0x04,0x04,0x05,0xA0,0x9B,0x01,0xF5,0x02,0xE5,0xEF,0x03,0xE1,0xE9,0x08,0x09,
0xAA,0xD4,0x01,0x9B,0xE3,0x02,0xF2,0x03,0xE5,0x04,0xF5,0xF9,0xE9,0x05,0xEF,0x06,
0x07,0xE1,0xE5,0x08,0xCE,0xA0,0xC6,0xF5,0x01,0x02,0x9B,0xC2,0x03,0xE1,0x04,0xEF,
0x05,0xE9,0x06,0x07,0x09,0x0A,0xE4,0xF3,0xE6,0xF6,0xF7,0xF0,0xF2,0x01,0xEC,0x02,
0x03,0xA0,0x9B,0x04,0x05,0xF5,0x06,0x07,0xEE,0x08,0x0B,0x0C,0xA0,0xF3,0xF9,0xAE,
0xD2,0xC7,0x01,0x9B,0x02,0xF5,0x03,0x04,0x05,0xE9,0xEC,0x06,0xE5,0x07,0xEF,0x08,
0xE1,0x09,0xF2,0x0A,0x01,0xF5,0x9B,0xD6,0x04,0x05,0xE8,0x9B,0x01,0xF5,0x02,0xE1,
0xE9,0xEF,0x03,0xE5,0x10,0x11,0xAA,0xEC,0xF1,0xAE,0xA0,0xF7,0xED,0xEE,0x01,0x02,
0x9B,0xEB,0x03,0x04,0x05,0x06,0xE3,0x07,0xEF,0x08,0xE9,0xF5,0x09,0xE1,0xE5,0xF0,
0xE8,0x0A,0x0B,0x0C,0x0D,0xF4,0x0E,0x0F,0xE8,0x0A,0xAD,0xCE,0x9B,0x01,0xD6,0x02,
0xF5,0xF7,0x03,0x04,0xE1,0xE5,0xE9,0x05,0xF2,0x06,0xEF,0x07,0x08,0x09,0xEE,0x03,
0xEC,0xAE,0x01,0x9B,0x02,0xF0,0x06,0xE9,0xA0,0xC3,0xEF,0x9B,0xE5,0x01,0x80,0x02,
0x03,0xE1,0x04,0x05,0x06,0x07,0xC6,0xD7,0x01,0x9B,0xF2,0x02,0x03,0xE8,0xE5,0xE1,
0x04,0xE9,0xEF,0x05,0x9B,0x9B,0x02,0xEF,0xE1,0x9B,0x01,0xE5,0x01,0xEF,0x9B,0xE1,
0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x19,0x1A,0x9B,0xBA,
0xE5,0xEA,0xF8,0x01,0x02,0xE6,0xA7,0x03,0xFA,0xE8,0x04,0xF7,0x05,0xF5,0xE2,0x06,
0xEB,0x07,0xF0,0x08,0x80,0xF6,0xE7,0x09,0xE4,0x0A,0xA0,0xE9,0x0B,0xE3,0xF9,0x0C,
0x0D,0xED,0x0E,0x0F,0xF3,0x10,0x11,0xEC,0x12,0xF4,0xF2,0x13,0xEE,0x14,0x15,0x16,
0x17,0x18,0x0A,0x0B,0xF3,0x9B,0xF5,0xE2,0x01,0x80,0xA0,0x02,0xE5,0xF2,0xE9,0x03,
0xEC,0x04,0xF9,0x05,0xEF,0x06,0xE1,0x07,0x08,0x09,0x10,0x11,0xC3,0xCC,0xC7,0x9B,
0xE3,0x01,0x80,0xEC,0xF9,0x02,0xF3,0x03,0xF5,0x04,0x05,0xF2,0x06,0xE9,0xA0,0x07,
0x08,0xEF,0xF4,0x09,0x0A,0xE1,0x0B,0xE8,0xEB,0xE5,0x0C,0x0D,0x0E,0x0F,0x0E,0x0F,
0xAE,0xF5,0xF7,0x01,0xEC,0x02,0xE4,0xE7,0xF2,0x03,0x9B,0xEF,0x04,0xF6,0x05,0x06,
0xF9,0xF3,0x07,0xE9,0xE1,0x08,0x09,0x80,0x0A,0x0B,0xE5,0x0C,0x0D,0xA0,0x1E,0x1F,
0x9B,0xA1,0xAD,0xE8,0xEA,0xF1,0xF5,0xFA,0x01,0x02,0x03,0x04,0xBA,0xF8,0xA7,0xE2,
0xE9,0x05,0x06,0x07,0xE6,0xED,0xE7,0xEB,0x08,0x09,0xF6,0xF0,0x0A,0xEF,0x0B,0xE3,
0x0C,0x0D,0x0E,0xF9,0x0F,0xE4,0xEC,0x10,0xE5,0x11,0xF4,0xF7,0x12,0x13,0xE1,0x14,
0x15,0x16,0xEE,0xF3,0x17,0x80,0x18,0x19,0xF2,0x1A,0x1B,0xA0,0x1C,0x1D,0xA0,0x0B,
0xF5,0x9B,0x01,0xEC,0xF3,0xF2,0x80,0xE1,0x02,0x03,0xF4,0xE9,0xEF,0xE6,0x04,0x05,
0x06,0x07,0xE5,0x08,0x09,0x0A,0x0F,0x10,0xBA,0xF9,0xA7,0xF4,0x9B,0x01,0xE7,0xEC,
0x02,0xEE,0x03,0xEF,0xF5,0x04,0xF2,0x05,0x06,0xE9,0x07,0xF3,0xE1,0x08,0x09,0x0A,
0x0B,0xE5,0x80,0x0C,0xE8,0xA0,0x0D,0x0E,0xE5,0x0D,0xE2,0xF5,0xF7,0x9B,0xEC,0x01,
0xF9,0xEE,0x02,0x03,0x04,0xF2,0x05,0x80,0x06,0xA0,0xE1,0xEF,0x07,0xF4,0xE9,0x08,
0x09,0x0A,0x0B,0x0C,0x15,0x16,0xA1,0xF8,0xE9,0xEB,0x01,0x80,0x9B,0xFA,0xE2,0x02,
0x03,0x04,0xA0,0xF0,0x05,0x06,0x07,0xE1,0x08,0xE6,0xF2,0xED,0xF6,0x09,0xE4,0x0A,
0xEF,0xF4,0xEC,0xF3,0xE7,0xE5,0x0B,0xE3,0x0C,0x0D,0x0E,0x0F,0x10,0x11,0x12,0x13,
0xEE,0x14,0xEF,0x01,0x9B,0xE1,0x0B,0x0C,0xD4,0xEF,0xE6,0xEC,0xF7,0xE1,0x01,0xBA,
0x02,0x9B,0xF9,0x03,0x04,0x05,0xF3,0x06,0x07,0x08,0xE9,0xA0,0x09,0x80,0xE5,0x0A,
0x15,0x16,0xA7,0xBA,0xE3,0xF7,0xF2,0xAD,0xE2,0x01,0x02,0x9B,0xE6,0x03,0xED,0xF6,
0x04,0xEB,0x05,0xF4,0x06,0x07,0x08,0xF3,0x09,0xF5,0x0A,0xEF,0x0B,0x0C,0x80,0xF9,
0xE1,0x0D,0xE4,0xE9,0xA0,0x0E,0x0F,0xEC,0xE5,0x10,0x11,0x12,0x13,0x14,0x0A,0x0B,
0xF9,0x9B,0xF5,0xF3,0x01,0x02,0xE2,0xED,0x80,0x03,0xF0,0xEF,0x04,0xA0,0x05,0xE9,
0x06,0xE1,0x07,0x08,0x09,0xE5,0x18,0x19,0xE2,0xEA,0xF2,0xE8,0xEC,0xED,0xFA,0x9B,
0x01,0xF5,0x02,0x03,0xF6,0x04,0xBA,0xE6,0x05,0x06,0xEB,0xEF,0x07,0xA7,0xF9,0x08,
0x09,0x0A,0x0B,0xE3,0x0C,0xEE,0xE1,0x0D,0xF3,0x0E,0xE9,0x0F,0x10,0xF4,0x80,0xE4,
0xE5,0x11,0x12,0xE7,0xA0,0x13,0x14,0x15,0x16,0x17,0x1B,0x1C,0xAE,0xFA,0xBF,0x01,
0xA7,0x9B,0x02,0xE9,0xF8,0xF9,0x03,0xE5,0xE8,0x04,0xE1,0xEB,0x05,0xE2,0x06,0x07,
0xE3,0x08,0xE7,0xF4,0x09,0x80,0xF6,0xF0,0x0A,0xE4,0x0B,0xF3,0xF7,0x0C,0x0D,0xEF,
0xEC,0xA0,0x0E,0x0F,0xED,0xE6,0x10,0xF5,0x11,0x12,0x13,0x14,0x15,0xF2,0x16,0xEE,
0x17,0x18,0x19,0x1A,0x0E,0x0F,0xED,0xA7,0x9B,0xE4,0x01,0xF9,0xF3,0xF2,0xF4,0x02,
0xE8,0x03,0xEC,0xF0,0x04,0xE1,0xE9,0x05,0x06,0x80,0xA0,0x07,0x08,0x09,0x0A,0xE5,
0xEF,0x0B,0x0C,0x0D,0x9B,0xF5,0x18,0x19,0xBA,0xAC,0xF6,0x9B,0xF0,0xE2,0x01,0xE6,
0x02,0xA7,0xAE,0xE7,0x03,0xE3,0xF5,0x04,0xED,0x05,0x06,0x07,0xEB,0x08,0x09,0xEE,
0xF2,0x0A,0xE4,0x0B,0xF9,0xEC,0x0C,0x0D,0xF4,0x80,0x0E,0xEF,0xF3,0xA0,0xE1,0x0F,
0xE9,0x10,0x11,0xE5,0x12,0x13,0x14,0x15,0x16,0x17,0x19,0x1A,0xA7,0xAC,0xBF,0xC3,
0xC8,0xE4,0xE6,0xED,0xF2,0xAE,0xEC,0xEE,0xF9,0x01,0x02,0x03,0x04,0xBA,0x05,0x9B,
0xF5,0x06,0x07,0x08,0x09,0xEB,0xF0,0x0A,0x0B,0x0C,0xE1,0xE3,0x0D,0xE8,0x0E,0x0F,
0xEF,0x10,0x11,0xF3,0x12,0xE9,0x13,0xE5,0x14,0x15,0xF4,0x16,0x17,0xA0,0x18,0x80,
0x14,0x15,0xBA,0xBF,0xE4,0xF7,0x9B,0xA7,0x01,0xEE,0x02,0x03,0x04,0xE3,0xE2,0xED,
0x05,0xF9,0x06,0xF4,0x07,0xEC,0x08,0xF5,0xF2,0x09,0xE1,0xF3,0x0A,0xEF,0x0B,0x0C,
0x0D,0xE9,0x80,0xE5,0x0E,0xA0,0x0F,0xE8,0x10,0x11,0x12,0x13,0x11,0x12,0xEB,0xFA,
0x80,0xE6,0x9B,0x01,0xA0,0x02,0x03,0xE9,0xE1,0x04,0xE4,0xF0,0xED,0xE2,0xE3,0xE7,
0xEC,0x05,0xE5,0x06,0x07,0x08,0x09,0xF4,0x0A,0x0B,0x0C,0xF3,0xEE,0x0D,0x0E,0xF2,
0x0F,0x10,0x04,0xE5,0xF3,0xEF,0x9B,0x01,0xE1,0x02,0x03,0xE9,0x0B,0x0C,0xA7,0xE2,
0xEC,0xE3,0xF2,0x01,0x9B,0x02,0x03,0x04,0xE9,0xEF,0xEE,0xE5,0xE1,0x80,0x05,0xA0,
0x06,0x07,0x08,0x09,0xF3,0x0A,0x05,0x06,0x9B,0xA0,0xE1,0xE5,0xE9,0x01,0x80,0xF0,
0x02,0xF4,0x03,0x04,0xA0,0x13,0xE3,0xAD,0xE4,0xE9,0xEE,0xEF,0xF0,0xF4,0xF6,0xA1,
0xE1,0xED,0x01,0xE2,0x02,0x03,0x04,0xA7,0x05,0x06,0xF7,0x07,0x9B,0xEC,0x08,0xE5,
0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0xF3,0x0F,0x10,0x11,0x80,0x12,0x05,0x06,0xE5,0xFA,
0xA0,0xF9,0x9B,0x01,0x80,0xE9,0x02,0xE1,0x03,0x04,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,
0x9B,0x9B,0x9B,0x9B
};

/*
 A/65b Table C7 Huffman Description Decode Tree (c) General Instruments Corp.

 not exact table, extracted from specs by me, faster more useful version
 since exact table is big endian but x86 isn't.

 byte offsets of character i tree root
*/
static unsigned int huffman2bo[128] = { 
0x0000, 0x002C, 0x002E, 0x0030, 0x0032, 0x0034, 0x0036, 0x0038, 
0x003A, 0x003C, 0x003E, 0x0040, 0x0042, 0x0044, 0x0046, 0x0048, 
0x004A, 0x004C, 0x004E, 0x0050, 0x0052, 0x0054, 0x0056, 0x0058, 
0x005A, 0x005C, 0x005E, 0x0060, 0x0062, 0x0064, 0x0066, 0x0068, 
0x006A, 0x00DE, 0x00E0, 0x00EA, 0x00EC, 0x00EE, 0x00F0, 0x00F2, 
0x00F8, 0x00FA, 0x00FC, 0x00FE, 0x0100, 0x0104, 0x0116, 0x0120, 
0x0122, 0x012C, 0x0132, 0x0138, 0x013C, 0x0140, 0x0144, 0x0146, 
0x014A, 0x014C, 0x0154, 0x0156, 0x0158, 0x015A, 0x015C, 0x015E, 
0x0160, 0x0162, 0x0176, 0x0184, 0x0194, 0x01A2, 0x01B2, 0x01BA, 
0x01C8, 0x01D2, 0x01DE, 0x01EA, 0x01F2, 0x01FC, 0x0208, 0x0210, 
0x021A, 0x0228, 0x022A, 0x0234, 0x024A, 0x025A, 0x025E, 0x0264, 
0x026E, 0x0270, 0x0272, 0x0274, 0x0276, 0x0278, 0x027A, 0x027C, 
0x027E, 0x0280, 0x02B4, 0x02CE, 0x02F0, 0x031A, 0x0358, 0x036E, 
0x038E, 0x03AC, 0x03D8, 0x03E0, 0x03F4, 0x0424, 0x0440, 0x0476, 
0x04AE, 0x04CE, 0x04D0, 0x0506, 0x0534, 0x0560, 0x0586, 0x0592, 
0x05AA, 0x05B8, 0x05DC, 0x05EC, 0x05EE, 0x05F0, 0x05F2, 0x05F4, 
};

#define DESCR_COZ 1525
/* character i order-1 trees */
static unsigned char huffman2co[1526] = { 
0x14,0x15,0x9B,0xD6,0xC9,0xCF,0xD7,0xC7,0x01,0xA2,0xCE,0xCB,0x02,0x03,0xC5,0xCC,
0xC6,0xC8,0x04,0xC4,0x05,0xC2,0x06,0xC3,0xD2,0x07,0xD3,0x08,0xCA,0xD4,0x09,0xCD,
0xD0,0x0A,0xC1,0x0B,0x0C,0x0D,0x0E,0x0F,0x10,0x11,0x12,0x13,0x9B,0x9B,0x9B,0x9B,
0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,
0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,
0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,
0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x38,0x39,0xAD,0xAF,0xB7,0xDA,
0xA8,0xB3,0xB5,0x01,0x02,0x9B,0xB4,0xF1,0xA2,0xD5,0xD6,0xD9,0x03,0x04,0x05,0xCF,
0x06,0xC9,0xF9,0xEA,0xEB,0xF5,0xF6,0x07,0x08,0x09,0xB2,0xC5,0xC6,0xB1,0x0A,0xEE,
0xCB,0x0B,0xD4,0x0C,0xC4,0xC8,0xD2,0x0D,0x0E,0x0F,0xC7,0xCA,0xCE,0xD0,0xD7,0x10,
0xC2,0x11,0xCC,0xEC,0xE5,0xE7,0x12,0xCD,0x13,0x14,0xC3,0x15,0x16,0x17,0xED,0x18,
0x19,0xF2,0x1A,0xD3,0x1B,0x1C,0xE4,0x1D,0xC1,0xE3,0x1E,0xE9,0xF0,0xE2,0xF7,0x1F,
0xF3,0xE6,0x20,0x21,0x22,0xE8,0xEF,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2A,0xF4,
0x2B,0x2C,0x2D,0x2E,0x2F,0xE1,0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x9B,0x9B,
0x03,0x04,0x80,0xAE,0xC8,0xD4,0x01,0x02,0x9B,0xA0,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,
0x9B,0x9B,0x02,0xF3,0xA0,0xF4,0x9B,0x01,0x9B,0x9B,0xAC,0x9B,0x9B,0x9B,0x9B,0x9B,
0x01,0xA0,0x9B,0xA2,0x07,0x08,0xE2,0xE4,0xE5,0xE6,0xA0,0xF2,0xE1,0x01,0x02,0xF3,
0xE3,0x03,0x04,0x05,0x9B,0x06,0x04,0x80,0xCA,0xD3,0xA2,0x01,0x9B,0x02,0x03,0xA0,
0x9B,0xA0,0x03,0x04,0x9B,0xB7,0xF4,0xA0,0xB0,0xF3,0x01,0x02,0xB9,0x02,0xB8,0x9B,
0xA0,0x01,0xAE,0x02,0xB6,0x9B,0x01,0xA0,0xA0,0x01,0x9B,0xB0,0xAE,0x01,0x9B,0xA0,
0xAE,0x01,0xA0,0x9B,0x9B,0x9B,0x9B,0x01,0xAC,0xAE,0x9B,0x9B,0x02,0x03,0x9B,0xA0,
0xB5,0xB6,0xB8,0x01,0x9B,0xA0,0x9B,0xA0,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0xA0,
0x9B,0x9B,0x08,0x09,0xE6,0xF5,0xF3,0xF4,0x9B,0xE4,0x01,0xED,0x02,0x03,0x04,0xF2,
0x05,0x06,0xEC,0xEE,0x07,0xA0,0x05,0x06,0x9B,0xEC,0xF5,0x01,0x02,0xE1,0xEF,0xE5,
0xE9,0xF2,0x03,0x04,0x06,0x07,0x9B,0xE9,0xF9,0xF2,0xF5,0x01,0x02,0x03,0xEC,0xEF,
0xE1,0x04,0xE8,0x05,0x05,0x06,0xF9,0xF2,0xF5,0x9B,0xE5,0xEF,0x01,0x02,0xE9,0xE1,
0x03,0x04,0x06,0x07,0xE1,0xE9,0xEE,0xF6,0xE4,0xEC,0xF3,0x01,0x02,0xF2,0x03,0x04,
0x9B,0x05,0x02,0x03,0xE5,0xEC,0x9B,0xEF,0x01,0xF2,0x05,0x06,0xF5,0xEF,0x9B,0xEC,
0xE9,0x01,0xE1,0xF2,0x02,0xE5,0x03,0x04,0x03,0x04,0x9B,0xE5,0xE9,0xF5,0xE1,0x01,
0xEF,0x02,0x04,0x05,0xA0,0xC9,0xF3,0x9B,0xAE,0xF2,0x01,0x02,0x03,0xEE,0xEF,0x05,
0x9B,0xAE,0xE9,0xE5,0x01,0xF5,0x02,0xE1,0x03,0x04,0xE5,0x03,0xE1,0xE9,0xF2,0x9B,
0x01,0x02,0x03,0x04,0x9B,0xE9,0xF5,0x01,0xE5,0x02,0xEF,0xE1,0xE1,0x05,0x9B,0xE3,
0xEF,0x01,0xF5,0xE5,0x02,0x03,0xE9,0x04,0xE5,0x03,0x9B,0xE9,0x01,0xE1,0xEF,0x02,
0x03,0x04,0xA7,0xEE,0xEC,0xF2,0xF3,0x01,0x9B,0x02,0xE1,0x06,0x9B,0xE8,0xE9,0x01,
0xF2,0xEC,0x02,0xEF,0x03,0xE5,0x04,0x05,0x9B,0x9B,0x03,0x04,0x9B,0xAE,0x01,0xE9,
0x02,0xE1,0xE5,0xEF,0x09,0x0A,0xF6,0xF9,0x01,0xAE,0xE3,0xE9,0xF5,0x9B,0xE5,0xEF,
0x02,0x03,0xE1,0x04,0xE8,0x05,0x06,0xF4,0x07,0x08,0xE8,0x07,0xE5,0xF7,0xD6,0xE1,
0x9B,0xE9,0xF2,0x01,0x02,0x03,0x04,0xEF,0x05,0x06,0xAE,0x01,0x9B,0xEE,0xE9,0x02,
0xE5,0x9B,0xA0,0x01,0x03,0x04,0x9B,0xE8,0xE5,0xE1,0xEF,0x01,0xE9,0x02,0x9B,0x9B,
0x9B,0xEF,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,
0x18,0x19,0xE8,0xEF,0xF8,0x9B,0xA7,0xF7,0xFA,0x01,0x02,0x03,0x04,0xE5,0xAE,0x05,
0xE6,0xE2,0x06,0xF6,0xEB,0xF5,0xE9,0x07,0xF0,0xF9,0xE7,0x08,0x09,0xE4,0x0A,0xE3,
0x0B,0xED,0x0C,0xF3,0x0D,0x0E,0x0F,0xEC,0x10,0xF4,0x11,0x12,0xF2,0xA0,0x13,0x14,
0x15,0xEE,0x16,0x17,0x0B,0x0C,0xE4,0xF3,0x9B,0xAE,0xE2,0x01,0x02,0x03,0xEC,0xA0,
0x04,0xE9,0xF2,0xF5,0x05,0xF9,0xE1,0x06,0xEF,0x07,0xE5,0x08,0x09,0x0A,0x0F,0x10,
0xF1,0xAE,0xC4,0xF9,0xAC,0x01,0xE3,0x02,0x9B,0xF2,0x03,0x04,0xA0,0xEC,0xF5,0x05,
0x06,0xE9,0x07,0xEB,0x08,0xF4,0x09,0xE5,0x0A,0xEF,0xE1,0xE8,0x0B,0x0C,0x0D,0x0E,
0x13,0x14,0xA7,0xBB,0xE6,0xED,0xF7,0xE7,0xF6,0x01,0x02,0x9B,0xEE,0x03,0x04,0xEC,
0x05,0xF5,0x06,0xAC,0xE4,0xF9,0xF2,0x07,0x08,0x09,0xAE,0x0A,0xEF,0x0B,0xE1,0xF3,
0x0C,0xE9,0x0D,0x0E,0x0F,0x10,0xE5,0x11,0x12,0xA0,0x1D,0x1E,0xA9,0xE8,0xF5,0x9B,
0x01,0xAD,0xBB,0xEB,0xFA,0x02,0xA7,0xE6,0xE2,0xE7,0x03,0x04,0x05,0x06,0xE9,0xF8,
0x07,0xAC,0xEF,0xF0,0x08,0xED,0xF6,0xF9,0x09,0xF7,0x0A,0x0B,0xAE,0x0C,0xE3,0x0D,
0xE5,0xF4,0x0E,0x0F,0xE4,0x10,0xEC,0x11,0xE1,0x12,0x13,0x14,0x15,0x16,0xEE,0xF3,
0x17,0x18,0xF2,0xA0,0x19,0x1A,0x1B,0x1C,0x09,0x0A,0xAE,0x9B,0xEC,0x01,0xF5,0x02,
0xF4,0xE6,0x03,0xE1,0xE5,0xE9,0x04,0xF2,0xEF,0x05,0x06,0x07,0xA0,0x08,0x0E,0x0F,
0xAD,0xE7,0x9B,0xA7,0xF9,0x01,0xEC,0x02,0xAC,0xF2,0x03,0xAE,0xF3,0xF5,0x04,0x05,
0xEF,0x06,0x07,0xE9,0xE1,0x08,0x09,0xE8,0x0A,0x0B,0xE5,0x0C,0xA0,0x0D,0x0D,0x0E,
0xA7,0xAC,0xF3,0xAD,0x01,0x02,0x9B,0xF9,0xF5,0xAE,0x03,0xEE,0x04,0xF2,0x05,0x06,
0xF4,0x07,0x08,0x09,0xEF,0xE1,0xA0,0x0A,0xE9,0x0B,0x0C,0xE5,0x14,0x15,0xAC,0xE2,
0xF8,0x9B,0xAE,0xFA,0x01,0xEB,0x02,0xA0,0x03,0x04,0xF0,0x05,0x06,0xE6,0xF6,0x07,
0xE4,0xED,0xE7,0x08,0xE1,0xEF,0xF2,0x09,0x0A,0x0B,0xEC,0x0C,0xE5,0xE3,0x0D,0xF4,
0x0E,0xF3,0x0F,0x10,0x11,0xEE,0x12,0x13,0x03,0xEF,0x9B,0xE1,0xE5,0xF5,0x01,0x02,
0x08,0x09,0xEC,0xF9,0xA7,0xEE,0x01,0xAC,0x9B,0xAE,0x02,0x03,0x04,0xF3,0x05,0xE9,
0x06,0xA0,0x07,0xE5,0x16,0x17,0xA7,0xAD,0xEE,0xE3,0xEB,0xF2,0x9B,0xE2,0x01,0x02,
0xF5,0x03,0xF4,0xAC,0x04,0x05,0xE6,0xED,0xF6,0x06,0xAE,0xF0,0x07,0x08,0xF3,0x09,
0x0A,0xE4,0x0B,0x0C,0xF9,0x0D,0xEF,0x0E,0xE1,0x0F,0x10,0xE9,0xEC,0x11,0xA0,0xE5,
0x12,0x13,0x14,0x15,0x0C,0x0D,0xA7,0xBB,0x9B,0x01,0xF9,0xAE,0xE2,0x02,0xED,0xF3,
0x03,0xF5,0xEF,0xF0,0x04,0x05,0xE9,0x06,0x07,0x08,0x09,0xA0,0xE1,0xE5,0x0A,0x0B,
0x19,0x1A,0xAD,0xBB,0xE2,0xEA,0xED,0xF2,0xFA,0xE6,0xEC,0x01,0x02,0x03,0x9B,0xF5,
0x04,0xA7,0xF6,0xF9,0x05,0x06,0xEB,0xEF,0x07,0x08,0x09,0x0A,0xAC,0x0B,0x0C,0xE3,
0xAE,0x0D,0xEE,0xE9,0x0E,0xE1,0x0F,0xF3,0x10,0x11,0xF4,0x12,0xE7,0xE5,0x13,0x14,
0xE4,0x15,0x16,0x17,0xA0,0x18,0x1A,0x1B,0xC2,0x9B,0xAD,0xAC,0xF8,0x01,0xAE,0x02,
0x03,0xE5,0xE7,0xE8,0xF9,0xE9,0xEB,0x04,0xE3,0xE1,0x05,0xF6,0x06,0xE4,0x07,0xE2,
0xF0,0x08,0x09,0xF3,0xF4,0xF7,0xEF,0x0A,0x0B,0x0C,0x0D,0xEC,0x0E,0x0F,0x10,0xF5,
0xED,0x11,0xE6,0xA0,0x12,0xF2,0x13,0x14,0x15,0xEE,0x16,0x17,0x18,0x19,0x0E,0x0F,
0xAD,0xED,0xF9,0x9B,0xAE,0x01,0xF3,0x02,0x03,0xF5,0xF4,0xF0,0x04,0xEF,0x05,0xE9,
0x06,0xE8,0xA0,0xE1,0xEC,0x07,0xF2,0x08,0xE5,0x09,0x0A,0x0B,0x0C,0x0D,0x9B,0xF5,
0x19,0x1A,0xA9,0xBB,0xF6,0xE6,0x01,0x9B,0xAD,0xE2,0xF0,0x02,0xA7,0x03,0x04,0x05,
0xF5,0xE3,0xAC,0xE7,0xF2,0x06,0xEB,0x07,0xEC,0xED,0xEE,0xF9,0x08,0xAE,0x09,0x0A,
0xE4,0x0B,0x0C,0xF4,0x0D,0xF3,0x0E,0x0F,0x10,0xE1,0xEF,0x11,0xE9,0x12,0x13,0xE5,
0x14,0xA0,0x15,0x16,0x17,0x18,0xA0,0x16,0xA2,0xA7,0xE2,0xEB,0xED,0xEE,0x9B,0xF7,
0x01,0x02,0x03,0xBB,0xF9,0xF0,0x04,0x05,0xEC,0x06,0x07,0x08,0xF5,0xE1,0x09,0xAC,
0xE3,0x0A,0xE8,0x0B,0xE9,0x0C,0xEF,0xF3,0xAE,0x0D,0x0E,0xE5,0x0F,0x10,0x11,0xF4,
0x12,0x13,0x14,0x15,0x14,0x15,0xBB,0xE2,0xAD,0xED,0x01,0x9B,0xA7,0xE3,0xAC,0xEC,
0xEE,0x02,0xF7,0x03,0x04,0xF9,0x05,0x06,0x07,0x08,0xF4,0xAE,0xF5,0x09,0x0A,0xF2,
0xE1,0xF3,0x0B,0x0C,0x0D,0xE9,0x0E,0x0F,0xEF,0xE5,0x10,0xA0,0xE8,0x11,0x12,0x13,
0x11,0x12,0xEF,0xF6,0x9B,0xEB,0xF9,0x01,0xA0,0xE2,0x02,0xE1,0x03,0xED,0x04,0xE3,
0xE9,0x05,0xE4,0xE5,0xE7,0x06,0xEC,0xF0,0x07,0x08,0x09,0x0A,0x0B,0xF3,0x0C,0xF4,
0xEE,0x0D,0xF2,0x0E,0x0F,0x10,0x05,0xE5,0xF3,0xF9,0x9B,0x01,0xEF,0x02,0x03,0xE1,
0x04,0xE9,0x0A,0x0B,0xAE,0x9B,0xEC,0xED,0x01,0x02,0xF3,0xEE,0xF2,0x03,0xE5,0x04,
0xE8,0xA0,0xE1,0x05,0xEF,0x06,0x07,0x08,0xE9,0x09,0x05,0x06,0xA0,0xAC,0xAD,0xF4,
0xE9,0x01,0x02,0xE1,0xE5,0x03,0x9B,0x04,0x11,0xA0,0xBF,0xE1,0xE2,0xE6,0xED,0xE4,
0xE9,0xF7,0xA7,0x01,0x02,0xBB,0x03,0x04,0xEC,0x05,0x9B,0xEE,0x06,0xEF,0x07,0xAC,
0xE5,0xF3,0x08,0x09,0x0A,0xAE,0x0B,0x0C,0x0D,0x0E,0x0F,0x10,0x06,0x07,0xA0,0xAE,
0xE1,0xE5,0xEC,0xFA,0x9B,0xEF,0xE9,0x01,0x02,0x03,0x04,0x05,0x9B,0x9B,0x9B,0x9B,
0x9B,0x9B,0x9B,0x9B,0x9B,0x9B,
};
/* 256 Unicode characters more than enough room to accumlate the segments. */
static char TextBuffer[1024]; 
static char SegmentBuffer[512];
static char DecompressionBuffer[512];
static iconv_t *Utf16ToUtf8CD;
static iconv_t *Ucs2ToUtf8CD;
static iconv_t *AsciiToUtf8CD;

static const char ATSCTEXT[] = "ATSCText";

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
int ATSCMultipleStringsInit(void)
{
    ObjectRegisterTypeDestructor(ATSCMultipleStrings_t, 
        (ObjectDestructor_t)ATSCMultipleStringsDestructor);
    Utf16ToUtf8CD = iconv_open("UTF-8", "UTF-16BE");
    if ((long) Utf16ToUtf8CD == -1)
    {
        return 1;
    }
    Ucs2ToUtf8CD = iconv_open("UTF-8", "UCS-2BE");
    if ((long) Ucs2ToUtf8CD == -1)
    {
        return 1;
    }
    AsciiToUtf8CD = iconv_open("ASCII", "UCS-2BE");
    if ((long) AsciiToUtf8CD == -1)
    {
        return 1;
    }    
    return 0;
}

void ATSCMultipleStringsDeInit(void)
{
    iconv_close(Utf16ToUtf8CD);
    iconv_close(Ucs2ToUtf8CD);
    iconv_close(AsciiToUtf8CD);    
}

ATSCMultipleStrings_t *ATSCMultipleStringsConvert(uint8_t *data, uint8_t len)
{
    ATSCMultipleStrings_t *result;
    int stringIndex;
    uint8_t *pos = data + 1;
    
    result = ObjectCreateType(ATSCMultipleStrings_t);
    result->number_of_strings = data[0];
    result->strings = calloc( result->number_of_strings, sizeof(ATSCString_t));
    LogModule(LOG_DEBUG, ATSCTEXT, "Start of conversion: Number of strings = %d\n", data[0]);
    for (stringIndex = 0; stringIndex < result->number_of_strings; stringIndex ++)
    {
        int segments;
        int segmentIndex;
        int sbIndex;
        bool supported = TRUE;
        
        result->strings[stringIndex].lang[0] = pos[0];
        result->strings[stringIndex].lang[1] = pos[1];        
        result->strings[stringIndex].lang[2] = pos[2];        
        segments = pos[3];
        pos += 4;

        TextBuffer[0] = 0;
        sbIndex = 0;
        LogModule(LOG_DEBUG, ATSCTEXT, "Number of segments = %d\n", segments);
        for (segmentIndex = 0; segmentIndex < segments; segmentIndex ++)
        {
            pos = AppendSegment(pos, &sbIndex, &supported);
        }

        if (supported)
        {
            /* Set strings[]->text to the decoded reassembled text. */
            result->strings[stringIndex].text = strdup(TextBuffer);
        }
        else
        {
            result->strings[stringIndex].text = NULL;
        }
        
    }
    LogModule(LOG_DEBUG, ATSCTEXT, "End of conversion\n");
    return result;    
}


/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/
static void ATSCMultipleStringsDestructor(ATSCMultipleStrings_t *strings)
{
    int i;
    for (i = 0; i < strings->number_of_strings; i ++)
    {
        free(strings->strings[i].text);
    }
    free(strings->strings);
}

static uint8_t *AppendSegment(uint8_t *segment, int *sbIndex, bool *supported)
{
    iconv_t textStandard = NULL;
    int compressionType = segment[0];
    int mode = segment[1];
    int numberBytes = segment[2];
    uint8_t *rawText;
    int i;
    
    size_t inBytesLeft;
    size_t outBytesLeft;
    char *outBytes;
    char *inBytes;
    size_t ret;    
        
    rawText = segment + 3;
    segment += 3 + numberBytes;
    LogModule(LOG_DEBUG, ATSCTEXT, "Segment: compressionType=%d mode=%d numberBytes=%d *sbIndex=%d\n",
        compressionType, mode, numberBytes, *sbIndex);
    switch (mode)
    {
        case 0x07 ... 0x08: /* Reserved */
        case 0x11 ... 0x1f: /* Reserved */
        case 0x28 ... 0x2f: /* Reserved */
        case 0x34 ... 0x3d: /* Reserved */
        case 0x3e:          /* Standard Compression Scheme for Unicode (SCSU) */
        case 0x40 ... 0x41: /* Assigned to ATSC standard for Taiwan */
        case 0x42 ... 0x47: /* Reserved for future ATSC use. */
        case 0x48:          /* Assigned to ATSC standard for South Korea */
        case 0x49 ... 0xdf: /* Reserved for future ATSC use. */
        case 0xe0 ... 0xfe: /* Used in other systems */
            *supported = FALSE;
            break;
        case 0xff:          /* Not applicable */
            textStandard = AsciiToUtf8CD;
            break;
        case 0x3f:
            textStandard = Utf16ToUtf8CD;
            break;
        default:
            textStandard =  Ucs2ToUtf8CD;
            break;
    }
   
    if (!*supported)
    {
        /* We still need to run though all the segments as there is no 
         * way to jump straight to the next string. 
         */
        return segment;
    }

    switch(compressionType)
    {
        case 0x00: /* No Compression */
            for (i = 0; i < numberBytes; i ++)
            {
                SegmentBuffer[(i * 2) + 0] = mode;
                SegmentBuffer[(i * 2) + 1] = rawText[i];
            }     
            inBytes = SegmentBuffer;
            inBytesLeft = numberBytes * 2;
            break;
        case 0x01: /* Huffman coding */
        case 0x02:
            HuffmanDecode((uint8_t*)&DecompressionBuffer, rawText, sizeof(DecompressionBuffer) - 1, numberBytes, compressionType);
            inBytes = DecompressionBuffer;
            inBytesLeft = strlen(DecompressionBuffer);
            break;
        default:
            *supported = FALSE;
            break;
    }
    

    if (!*supported)
    {
        /* We still need to run though all the segments as there is no 
         * way to jump straight to the next string. 
         */
        return segment;
    }

    /* Convert using iconv */
    outBytesLeft = sizeof(TextBuffer) - *sbIndex;
    outBytes = TextBuffer + *sbIndex;

    ret = iconv(textStandard, &inBytes, &inBytesLeft, &outBytes, &outBytesLeft);
    if (ret != -1)
    {
        *outBytes = 0;
        sbIndex += (long)outBytes - (long)(TextBuffer + *sbIndex);
    }
    
    return segment;
}

/*
 It's a binary tree lookup for common letter combinations, used for
 EIT Event Title and ETT Event Description Text compression modes.
 *d destination, dlen destination max length,
 *s source, slen source length
 comp is 1 for title decode, 2 for description decode
 This was fun to write.
*/
static void HuffmanDecode(uint8_t *dest, uint8_t *src, int destLen, int srcLen, int comp)
{
    uint8_t p = 0;
    uint8_t c = 0;
    uint8_t o =0;
    uint8_t b = 0;
    uint8_t *co;
    unsigned int i, j, k, to, zo, z, *bo;
   
    if (comp == 1)
    {
        bo = huffman1bo;        /* byte offset of char p tree root */
        co = huffman1co;        /* char p order-1 tree */
        z = TITLE_COZ;
    }
    else
    {
        bo = huffman2bo;
        co = huffman2co;
        z = DESCR_COZ;
    }

    /* should be pointing to correct tables now */
    for (i=0; i < (srcLen<<3); i++)
    {
        if (p > 127)
        { 
            return;
        }
        /* get tree offset for char p from order-1 tree byte offset table */
        to = bo[ p ];

        /* direction in tree from bit in compressed string */
        /* compressed bit sets to left(0) or right(1) for next branch or leaf */
        b = src[ i >> 3 ] & (1 << (~i & 7));

        /* force comparison to binary */
        if (b != 0)
        {
            b = 1;
        }

        /* minimum of two linked-list lookups for shortest first order entry 
           such as common following letters, but most will be multiple lookups
        */

        zo = to + (o << 1) + b;

        /* sanity check, don't stray outside table */
        if (zo > z) 
        { 
            
            return;     
        }

        /* first entry has tree lookup to first order left/right choice tree */
        o = co[ zo ]; /* tree root offset is anchor for branches */

        /* top bit set means it's a leaf char. this implies that */
        /* branches will loop until it is a leaf char */
        if (0 != (0x80 & o) )
        {
            c = 0x7F & o;

            if ( c == 27 )
            {
                /* handle Escape to 8 bit mode */
                i++; /* point to msb of uncompressed byte */
                j = i & 7;
                k = 8 - j;
                /* get current byte */
                c = src[ i >> 3 ];
                /* shift needed? */
                if (0 != j)
                {
                    c <<= j;
                    b = src[ (i >> 3) + 1];
                    b >>= k;
                    c |= b;
                }
                i += 7; /* skip past lsb of uncompressed byte */
            }

            p = c; /* c leaf becomes new index for order-1 tree root offset */
            o = 0; /* clear offset to order-1 tree */
            *dest = c;
            destLen--;

            /* out of space gets nul term and exits */
            if (destLen < 1) 
            {
                *dest = 0;
                break;
            }

            dest++; /* else move to next char */

            /* nul term exits */
            if ( c == 0 )
            {
                break;
            }
        }
    }
}

