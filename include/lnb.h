/* From linuxtv.org's szap utility, just cleaned up a bit and made more dvbstreamer like. */
#ifndef _LNB_H
#define _LNB_H

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

#endif
