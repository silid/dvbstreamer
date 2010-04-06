/*
 * This code originate from Mythtv and was simply converted to use char * rather 
 * than QString.
 * Many thanks to the Mythtv developers for working this out :-)
 */
#include <stdlib.h>
#include <string.h>
#include "freesat_huffman.h"

struct fsattab {
    unsigned int value;
    short bits;
    char next;
};

#define START   '\0'
#define STOP    '\0'
#define ESCAPE  '\1'

#include "freesat_tables.h"

#include "types.h"

char *freesat_huffman_to_string(const unsigned char *src, uint size)
{
    struct fsattab *fsat_table;
    unsigned int *fsat_index;

    if (src[1] == 1 || src[1] == 2)
    {
        if (src[1] == 1)
        {
            fsat_table = fsat_table_1;
            fsat_index = fsat_index_1;
        } 
        else 
        {
            fsat_table = fsat_table_2;
            fsat_index = fsat_index_2;
        }

        char *uncompressed = calloc((size * 3) + 1, sizeof(char));
        size_t uncompressedSize = size * 3;
        int p = 0;
        unsigned value = 0, byte = 2, bit = 0;

        while (byte < 6 && byte < size)
        {
            value |= src[byte] << ((5-byte) * 8);
            byte++;
        }

        char lastch = START;
        do
        {
            bool found = FALSE;
            unsigned bitShift = 0;
            char nextCh = STOP;
            
            if (lastch == ESCAPE)
            {
                found = TRUE;
                // Encoded in the next 8 bits.
                // Terminated by the first ASCII character.
                nextCh = (value >> 24) & 0xff;
                bitShift = 8;
                if ((nextCh & 0x80) == 0)
                {
                    if (nextCh < ' ')
                    {
                        nextCh = STOP;
                    }
                    lastch = nextCh;
                }
            }
            else
            {
                unsigned indx = (unsigned)lastch;
                unsigned j;
                
                for (j = fsat_index[indx]; j < fsat_index[indx+1]; j++)
                {
                    unsigned mask = 0, maskbit = 0x80000000;
                    short kk;
                    for (kk = 0; kk < fsat_table[j].bits; kk++)
                    {
                        mask |= maskbit;
                        maskbit >>= 1;
                    }
                    
                    if ((value & mask) == fsat_table[j].value)
                    {
                        nextCh = fsat_table[j].next;
                        bitShift = fsat_table[j].bits;
                        found = TRUE;
                        lastch = nextCh;
                        break;
                    }
                }
            }
            if (found)
            {
                if ((nextCh != STOP) && (nextCh != ESCAPE))
                {
                    if (p >= uncompressedSize)
                    {
                        uncompressed = realloc(uncompressed, p + 10 + 1);
                        uncompressedSize = p + 10;
                    }
                    uncompressed[p++] = nextCh;
                }
                // Shift up by the number of bits.
                unsigned b;
                for (b = 0; b < bitShift; b++)
                {
                    value = (value << 1) & 0xfffffffe;

                    if (byte < size)
                    {
                        value |= (src[byte] >> (7-bit)) & 1;
                    }
                    
                    if (bit == 7)
                    {
                        bit = 0;
                        byte++;
                    }
                    else 
                    {
                        bit++;
                    }
                }
            }
            else
            {
                // Entry missing in table.
                if (p + 3 > uncompressedSize)
                {
                    uncompressed = realloc(uncompressed, p + 3 + 1);
                }
                strcpy(&uncompressed[p], "...");
                return uncompressed;
            }
        } while (lastch != STOP && byte < size+4);

        uncompressed[p] = 0;
        return uncompressed;
    }
    return "";
}
