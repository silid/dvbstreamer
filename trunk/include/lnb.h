/* From linuxtv.org's szap utility, just cleaned up a bit and made more dvbstreamer like. */
#ifndef _LNB_H
#define _LNB_H
#include "types.h"

/**
 * Structure describing an LNB type.
 */
typedef struct LNBInfo_s {
    char    *name;              /**< Name of the LNB type.*/
    char    **desc;             /**< Description of this LNB.*/
    unsigned long   lowFrequency;    /**< Low band LO frequency. */
    unsigned long   highFrequency;   /**< zero indicates no hiband */
    unsigned long   switchFrequency; /**< zero indicates no hiband */
}LNBInfo_t;

/** 
 * Enumerate through standard types of LNB's until NULL returned.
 * Increment curno each time.
 */
LNBInfo_t *LNBEnumerate(int curno);

/**
 * Decode an lnb type, for example given on a command line
 * If alpha and standard type, e.g. "Universal" then match that
 * otherwise low[,high[,switch]]
 */
int LNBDecode(char *str, LNBInfo_t *lnb);

/**
 * Convert a transponder frequency to the intermediate frequency to use with the
 * specified LNB.
 * @param info The LNB to use to convert the transponder frequency.
 * @param freq The transponder frequency to convert.
 * @param tone Pointer to a bool to store whether the 22Khz tone should be enabled for this 
 *             transponder.
 * @return The intermediate frequency to tune to.
 */
unsigned long LNBTransponderToIntermediateFreq(LNBInfo_t *info, unsigned long freq, bool *tone);

#endif
