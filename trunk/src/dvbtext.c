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

dvbtext.c

Convert DVB text to UTF-8.

Note: A lot of this code is taken from the tvgrab_dvb source!
    http://www.darkskiez.co.uk/index.php?page=tv_grab_dvb
*/
#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include <iconv.h>

#include "logging.h"
#include "dvbtext.h"


/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/
enum CS {
    CS_UNKNOWN,
    ISO6937,
    ISO8859_5,
    ISO8859_6,
    ISO8859_7,
    ISO8859_8,
    ISO8859_9,
    ISO10646,
    CS_OTHER,
};

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static enum CS cs_old = CS_UNKNOWN;
static iconv_t cd;
static pthread_mutex_t ResultBufferMutex = PTHREAD_MUTEX_INITIALIZER;
static char ResultBuffer[256 * 6];
static char UTF8[] = "UTF-8";
static char DVBTEXT[] = "DVBText";

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/

char *DVBTextToUTF8(char *toConvert, size_t toConvertLen)
{
    enum CS cs_new = CS_UNKNOWN;
    char asciiBuffer[256]; /* Used when no ISO6937 conversion is provided. */
    size_t inBytesLeft;
    size_t outBytesLeft;
    char *outBytes;
    char *inBytes;
    size_t ret;
    char *result;

    switch ((unsigned char)*toConvert) 
    {
        /* if the first byte of the text field has a value in the range of "0x20"
           to "0xFF" then this and all subsequent bytes in the text item are 
           coded using the default coding table (table 00 - Latin alphabet) of 
           figure A.1
        */
        case 0x20 ... 0xFF: 
            cs_new = ISO6937;
            break;
        /* if the first byte of the text field is in the range "0x01" to "0x05" 
           then the remaining bytes in the text item are coded in accordance with
           character coding table 01 to 05 respectively, which are given in 
           figures A.2 to A.6 respectively
        */
        case 0x01: 
            cs_new = ISO8859_5;
            toConvert += 1;
            break;
        case 0x02:
            cs_new = ISO8859_6;
            toConvert += 1;
            break;
        case 0x03:
            cs_new = ISO8859_7;
            toConvert += 1;
            break;
        case 0x04:
            cs_new = ISO8859_8;
            toConvert += 1;
            break;
        case 0x05:
            cs_new = ISO8859_9;
            toConvert += 1;
            break;

        /* if the first byte of the text field has a value "0x10" then the 
           following two bytes carry a 16-bit value (uimsbf) N to indicate that
           the remaining data of the text field is coded using the character code
           table specified by ISO Standard 8859, Parts 1 to 9.
        */
        case 0x10:
            
            cs_new = CS_OTHER
                + ((unsigned char)toConvert[1] << 8)
                +  (unsigned char)toConvert[2];
            toConvert += 3;
            break;
        /* if the first byte of the text field has a value "0x11" then the 
           ramaining bytes in the text item are coded in pairs in accordance with
           the Basic Multilingual Plane of ISO/IEC 10646-1
        */
        case 0x11: 
            cs_new = ISO10646;
            toConvert += 1;
            break;
        /* Values for the first byte of "0x00", "0x06" to "0x0F", and "0x12" to 
           "0x1F" are reserved for future use.
        */
        case 0x06 ... 0x0F: case 0x12 ... 0x1F: 
            LogModule(LOG_ERROR, DVBTEXT, "Reserved encoding: %02x\n", *toConvert);
            return NULL;

        case 0x00:
            return "";
    }
    pthread_mutex_lock(&ResultBufferMutex);    
    LogModule(LOG_DEBUG, DVBTEXT, "Selected %d to convert to utf-8\n", cs_new);
    if ((cs_old != cs_new) || (cs_old == CS_UNKNOWN))
    {
        if (cd) 
        {
            LogModule(LOG_DEBUG, DVBTEXT, "Closing previous conversion descriptor.\n");
            iconv_close(cd);
            cd = NULL;
        }
        LogModule(LOG_DEBUG, DVBTEXT, "Opening new conversion descriptor.\n");
        switch (cs_new) 
        {
            case ISO6937:

                cd = iconv_open(UTF8, "ISO6937");
                if ((long)cd == -1)
                {
                    int toConvertIndex, asciiIndex = 0;
                    /* libiconv doesn't support ISO 6937, but glibc does?! 
                       so fall back to ISO8859-1 and strip out the non spacing 
                       diacritical mark/graphical characters etc.
                    */
                    for (toConvertIndex = 0; toConvertIndex < toConvertLen; toConvertIndex ++)
                    {
                        unsigned char ch = (unsigned char)toConvert[toConvertIndex];
                        if (ch < 128)
                        {
                            asciiBuffer[asciiIndex] = (char)ch;
                            asciiIndex ++;
                        }
                    }
                    toConvert = asciiBuffer;
                    toConvertLen = asciiIndex;
                    cd = iconv_open(UTF8, "ISO8859-1");
                }

                break;
            case ISO8859_5:
                cd = iconv_open(UTF8, "ISO8859-5");
                break;
            case ISO8859_6:
                cd = iconv_open(UTF8, "ISO8859-6");
                break;
            case ISO8859_7:
                cd = iconv_open(UTF8, "ISO8859-7");
                break;
            case ISO8859_8:
                cd = iconv_open(UTF8, "ISO8859-8");
                break;
            case ISO8859_9:
                cd = iconv_open(UTF8, "ISO8859-9");
                break;
            case ISO10646:
                cd = iconv_open(UTF8, "ISO-10646/UTF8");
                break;
            default: 
            {
                char from[14];
                int i = cs_new - CS_OTHER;
                snprintf(from, sizeof(from), "ISO8859-%d", i);
                cd = iconv_open(UTF8, from);
            }
        }
        cs_old = cs_new;
    }
    if ((long)cd == -1)
    {
        LogModule(LOG_INFO, DVBTEXT, "Failed to open conversion descriptor!\n");
        return NULL;
    }
    inBytes = toConvert;
    inBytesLeft = toConvertLen;
    outBytesLeft = sizeof(ResultBuffer);
    outBytes = ResultBuffer;

    LogModule(LOG_DEBUG,DVBTEXT,  "Starting conversion. (%d, %d, %d)\n", cd , inBytesLeft, outBytesLeft);
    ret = iconv(cd, (ICONV_INPUT_CAST)&inBytes, &inBytesLeft, &outBytes, &outBytesLeft);

    if (ret == -1)
    {
        LogModule(LOG_DEBUG, DVBTEXT, "Conversion failed.\n");
        pthread_mutex_unlock(&ResultBufferMutex);    
        return NULL;
    }
    *outBytes = 0;
    result = strdup(ResultBuffer);
    pthread_mutex_unlock(&ResultBufferMutex);
    LogModule(LOG_DEBUG, DVBTEXT, "Conversion successful.\n");
    return result;
}

