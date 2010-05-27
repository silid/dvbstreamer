/* From linuxtv.org's szap utility, just cleaned up a bit and made more dvbstreamer like. */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "lnb.h"

static char *univ_desc[] = {
        "Europe",
        "10800 to 11800 MHz and 11600 to 12700 Mhz",
        "Dual LO, loband 9750, hiband 10600 MHz",
        (char *)NULL };

static char *dbs_desc[] = {
        "Expressvu, North America",
        "12200 to 12700 MHz",
        "Single LO, 11250 MHz",
        (char *)NULL };

static char *standard_desc[] = {
        "10945 to 11450 Mhz",
        "Single LO, 10000 Mhz",
        (char *)NULL };

static char *enhan_desc[] = {
        "Astra",
        "10700 to 11700 MHz",
        "Single LO, 9750 MHz",
        (char *)NULL };

static char *cband_desc[] = {
        "Big Dish",
        "3700 to 4200 MHz",
        "Single LO, 5150 Mhz",
        (char *)NULL };

static LNBInfo_t LNBs[] = {
    {"UNIVERSAL",   univ_desc,       9750000, 10600000, 11700000 },
    {"DBS",         dbs_desc,       11250000,        0,        0 },
    {"STANDARD",    standard_desc,  10000000,        0,        0 },
    {"ENHANCED",    enhan_desc,      9750000,        0,        0 },
    {"C-BAND",      cband_desc,      5150000,        0,        0 }
};

/* Enumerate through standard types of LNB's until NULL returned.
 * Increment curno each time
 */

LNBInfo_t * LNBEnumerate(int curno)
{
    if (curno >= (int) (sizeof(LNBs) / sizeof(LNBs[0])))
    {
        return NULL;
    }
    return &LNBs[curno];
}

/* Decode an lnb type, for example given on a command line
 * If alpha and standard type, e.g. "Universal" then match that
 * otherwise low[,high[,switch]]
 */

int LNBDecode(char *str, LNBInfo_t *lnb)
{
    int i;
    char *cp, *np;

    memset(lnb, 0, sizeof(LNBInfo_t));
    cp = str;
    
    while(*cp && isspace(*cp))
    {
        cp++;
    }
    
    if (isalpha(*cp)) 
    {
        for (i = 0; i < (int)(sizeof(LNBs) / sizeof(LNBs[0])); i++) 
        {
            if (!strcasecmp(LNBs[i].name, cp)) 
            {
                *lnb = LNBs[i];
                return 0;
            }
        }
        return 1;
    }
    if (*cp == '\0' || !isdigit(*cp))
    {
        return 0;
    }
    
    lnb->lowFrequency = strtoul(cp, &np, 0);
    if (lnb->lowFrequency == 0)
    {
        return 1;
    }
    
    cp = np;
    while(*cp && (isspace(*cp) || *cp == ','))
    {
        cp++;
    }
    if (*cp == '\0')
    {
        return 0;
    }
    
    if (!isdigit(*cp))
    {
        return 1;
    }
    
    lnb->highFrequency = strtoul(cp, &np, 0);
    cp = np;
    
    while(*cp && (isspace(*cp) || *cp == ','))
    {
        cp++;
    }
    if (*cp == '\0')
    {
        return 0;
    }
    
    if (!isdigit(*cp))
    {
        return 1;
    }
    
    lnb->switchFrequency = strtoul(cp, NULL, 0);
    return 0;
}

unsigned long LNBTransponderToIntermediateFreq(LNBInfo_t *info, unsigned long freq, bool *tone)
{
    bool hiband = FALSE;
    unsigned long ifreq = 0;
    *tone = FALSE;

    if (info->switchFrequency&& info->highFrequency&&
        (freq >= info->switchFrequency))
    {
        hiband = TRUE;
    }

    if (hiband)
    {
      ifreq = freq - info->highFrequency;
      *tone = TRUE;
    }
    else
    {
      if (freq < info->lowFrequency)
      {
          ifreq = info->lowFrequency- freq;
      }
      else
      {
          ifreq = freq - info->lowFrequency;
      }
      *tone = FALSE;
    }
    return ifreq;
}