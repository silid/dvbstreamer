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

parsezap.c

Parse channels.conf file and add services to the database.

Majority of the parsing code taken from the xine input_dvb plugin code.

*/
#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "multiplexes.h"
#include "services.h"
#include "logging.h"
#include "dvbadapter.h"
/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/

typedef struct
{
    char *name;
    int value;
}
Param;

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/

static int find_param(const Param *list, const char *name);
static int findMultiplex(fe_type_t fe_type, int freq, DVBDiSEqCSettings_t *diseqcsettings, int *uid);
static int parsezapline(char * str, fe_type_t fe_type);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/

static const Param inversion_list [] =
    {
        { "INVERSION_OFF", INVERSION_OFF },
        { "INVERSION_ON", INVERSION_ON },
        { "INVERSION_AUTO", INVERSION_AUTO },
        { NULL, 0 }
    };

static const Param bw_list [] =
    {
        { "BANDWIDTH_6_MHZ", BANDWIDTH_6_MHZ },
        { "BANDWIDTH_7_MHZ", BANDWIDTH_7_MHZ },
        { "BANDWIDTH_8_MHZ", BANDWIDTH_8_MHZ },
        { "BANDWIDTH_AUTO", BANDWIDTH_AUTO },
        { NULL, 0 }
    };

static const Param fec_list [] =
    {
        { "FEC_AUTO", FEC_AUTO },
        { "FEC_1_2", FEC_1_2 },
        { "FEC_2_3", FEC_2_3 },
        { "FEC_3_4", FEC_3_4 },
        { "FEC_4_5", FEC_4_5 },
        { "FEC_5_6", FEC_5_6 },
        { "FEC_6_7", FEC_6_7 },
        { "FEC_7_8", FEC_7_8 },
        { "FEC_8_9", FEC_8_9 },
        { "FEC_NONE", FEC_NONE },
        { NULL, 0 }
    };

static const Param guard_list [] =
    {
        {"GUARD_INTERVAL_1_16", GUARD_INTERVAL_1_16 },
        {"GUARD_INTERVAL_1_32", GUARD_INTERVAL_1_32 },
        {"GUARD_INTERVAL_1_4", GUARD_INTERVAL_1_4 },
        {"GUARD_INTERVAL_1_8", GUARD_INTERVAL_1_8 },
        {"GUARD_INTERVAL_AUTO", GUARD_INTERVAL_AUTO },
        { NULL, 0 }
    };

static const Param hierarchy_list [] =
    {
        { "HIERARCHY_NONE", HIERARCHY_NONE },
        { "HIERARCHY_1", HIERARCHY_1 },
        { "HIERARCHY_2", HIERARCHY_2 },
        { "HIERARCHY_4", HIERARCHY_4 },
        { "HIERARCHY_AUTO", HIERARCHY_AUTO },            
        { NULL, 0 }
    };

static const Param modulation_list [] =
    {
        { "QPSK", QPSK },
        { "QAM_16", QAM_16 },
        { "QAM_32", QAM_32 },
        { "QAM_64", QAM_64 },
        { "QAM_128", QAM_128 },
        { "QAM_256", QAM_256 },
        { "QAM_AUTO", QAM_AUTO },
#if defined(ENABLE_ATSC)            
        { "8VSB", VSB_8 },
        { "16VSB", VSB_16 },
#endif            
        { NULL, 0 }
    };

static const Param transmissionmode_list [] =
    {
        { "TRANSMISSION_MODE_2K", TRANSMISSION_MODE_2K },
        { "TRANSMISSION_MODE_8K", TRANSMISSION_MODE_8K },
        { "TRANSMISSION_MODE_AUTO", TRANSMISSION_MODE_AUTO },
        { NULL, 0 }
    };
static const char PARSEZAP[] = "ParseZap";

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
int parsezapfile(char *path, fe_type_t fe_type)
{
    FILE      *f;
    char       str[255];
    int        result;

    f = fopen(path, "rb");
    if (!f)
    {
        LogModule(LOG_ERROR, PARSEZAP, "Failed to open dvb channel file '%s'\n", path);
        return 0;
    }

    while ( fgets (str, sizeof(str), f))
    {
        result = parsezapline(str, fe_type);
    }

    return 1;
}

/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/

static int find_param(const Param *list, const char *name)
{
    while (list->name && strcmp(list->name, name))
        list++;
    return list->value;;
}

static int findMultiplex(fe_type_t fe_type, int freq, DVBDiSEqCSettings_t *diseqcsettings, int *uid)
{
    int notFound = 1;
    MultiplexEnumerator_t enumerator = MultiplexEnumeratorGet();
    Multiplex_t *multiplex;
    do
    {
        multiplex = MultiplexGetNext(enumerator);
        if (multiplex)
        {
            struct dvb_frontend_parameters feparams;
            DVBDiSEqCSettings_t muxdiseqcsettings;

            MultiplexFrontendParametersGet(multiplex, &feparams, &muxdiseqcsettings);

            if (feparams.frequency == freq)
            {
                if (fe_type == FE_QPSK)
                {
                    if ((muxdiseqcsettings.polarisation == diseqcsettings->polarisation) &&
                        (muxdiseqcsettings.satellite_number == diseqcsettings->satellite_number))
                    {
                        *uid = multiplex->uid;
                        notFound = 0;
                    }
                }
                else
                {
                    *uid = multiplex->uid;
                    notFound = 0;
                }
            }
            
            MultiplexRefDec(multiplex);
        }
    }while(multiplex && notFound);
    MultiplexEnumeratorDestroy(enumerator);
    return notFound;
}

static int parsezapline(char * str, fe_type_t fe_type)
{
    /*
        try to extract channel data from a string in the following format
        (DVBS) QPSK: <channel name>:<frequency>:<polarisation>:<sat_no>:
                        <sym_rate>:<vpid>:<apid>:<service id>
        (DVBC) QAM:  <channel name>:<frequency>:<inversion>:<sym_rate>:<fec>:
                        <qam>:<vpid>:<apid>:<service id>
        (DVBT) OFDM: <channel name>:<frequency>:<inversion>:<bw>:<fec_hp>:
                        <fec_lp>:<qam>:<transmissionm>:<guardlist>:
                        <hierarchinfo>:<vpid>:<apid>:<service id>
        (ATSC) VSB:  <channel name>:<frequency>:<modulation>:<vpid>:<apid>:
                        <service id>
                        
        <channel name> = any string not containing ':'
        <frequency>    = unsigned long
        <polarisation> = 'v' or 'h'
        <sat_no>       = unsigned long, usually 0 :D
        <sym_rate>     = symbol rate in MSyms/sec


        <inversion>    = INVERSION_ON | INVERSION_OFF | INVERSION_AUTO
        <fec>          = FEC_1_2, FEC_2_3, FEC_3_4 .... FEC_AUTO ... FEC_NONE
        <qam>          = QPSK, QAM_128, QAM_16 ...

        <bw>           = BANDWIDTH_6_MHZ, BANDWIDTH_7_MHZ, BANDWIDTH_8_MHZ
        <fec_hp>       = <fec>
        <fec_lp>       = <fec>
        <transmissionm> = TRANSMISSION_MODE_2K, TRANSMISSION_MODE_8K
        <vpid>         = video program id
        <apid>         = audio program id
        <service id>   = MPEG2 program id/DVB service id

    */
    unsigned long freq;
    char *field, *tmp;
    struct dvb_frontend_parameters   front_param;
    DVBDiSEqCSettings_t diseqcsettings;
    char *name;
    int id;
    int source;
    int muxUID;

    tmp = str;
#define NEXTFIELD() if(!(field = strsep(&tmp, ":")))return -1

    /* find the channel name */
    NEXTFIELD();
    name = strdup(field);

    /* find the frequency */
    NEXTFIELD();
    freq = strtoul(field,NULL,0);


    switch(fe_type)
    {
        case FE_QPSK:

            front_param.frequency = freq * 1000;
            front_param.inversion = INVERSION_AUTO;
            /* find out the polarisation */
            NEXTFIELD();
            diseqcsettings.polarisation = (field[0] == 'h' ? POL_HORIZONTAL: POL_VERTICAL);

            /* satellite number */
            NEXTFIELD();
            diseqcsettings.satellite_number = strtoul(field, NULL, 0);

            /* symbol rate */
            NEXTFIELD();
            front_param.u.qpsk.symbol_rate = strtoul(field, NULL, 0) * 1000;

            front_param.u.qpsk.fec_inner = FEC_AUTO;
        break;
        case FE_QAM:
            front_param.frequency = freq;

            /* find out the inversion */
            NEXTFIELD();
            front_param.inversion = find_param(inversion_list, field);

            /* find out the symbol rate */
            NEXTFIELD();
            front_param.u.qam.symbol_rate = strtoul(field, NULL, 0);

            /* find out the fec */
            NEXTFIELD();
            front_param.u.qam.fec_inner = find_param(fec_list, field);

            /* find out the qam */
            NEXTFIELD();
            front_param.u.qam.modulation = find_param(modulation_list, field);
        break;
        case FE_OFDM:
            /* DVB-T frequency is in kHz - workaround broken channels.confs */
            if (freq < 1000000)
            {
                freq*=1000;
            }
            front_param.frequency = freq;

            /* find out the inversion */
            NEXTFIELD();
            front_param.inversion = find_param(inversion_list, field);

            /* find out the bandwidth */
            NEXTFIELD();
            front_param.u.ofdm.bandwidth = find_param(bw_list, field);

            /* find out the fec_hp */
            NEXTFIELD();
            front_param.u.ofdm.code_rate_HP = find_param(fec_list, field);

            /* find out the fec_lp */
            NEXTFIELD();
            front_param.u.ofdm.code_rate_LP = find_param(fec_list, field);

            /* find out the qam */
            NEXTFIELD();
            front_param.u.ofdm.constellation = find_param(modulation_list, field);

            /* find out the transmission mode */
            NEXTFIELD();
            front_param.u.ofdm.transmission_mode = find_param(transmissionmode_list, field);

            /* guard list */
            NEXTFIELD();
            front_param.u.ofdm.guard_interval = find_param(guard_list, field);

            NEXTFIELD();
            front_param.u.ofdm.hierarchy_information = find_param(hierarchy_list, field);
            break;
#if defined(ENABLE_ATSC)            
        case FE_ATSC:
            front_param.frequency = freq;
            front_param.inversion = INVERSION_AUTO;            
            NEXTFIELD();
            front_param.u.vsb.modulation = find_param(modulation_list, field);
            break;
#endif            
        default:
            break;
    }

    if (findMultiplex(fe_type, front_param.frequency, &diseqcsettings, &muxUID))
    {
        LogModule(LOG_DEBUGV, PARSEZAP, "Adding frequency %d (type %d)\n", front_param.frequency, fe_type);
        MultiplexAdd(fe_type, &front_param, &diseqcsettings, &muxUID);
    }

    /* Video PID - not used */
    NEXTFIELD();
    /* Audio PID - not used */
    NEXTFIELD();
    /* service ID */
    NEXTFIELD();
    id = strtoul(field, NULL, 0);
    if (fe_type == FE_ATSC)
    {
        source = -1;
    }
    else
    {
        source = id;
    }
    LogModule(LOG_DEBUGV, PARSEZAP, "Adding service \"%s\" %d\n", name, id);
    if (ServiceAdd(muxUID, name, id, source, FALSE, ServiceType_Unknown, -1, 0x1fff, -1))
    {
        LogModule(LOG_ERROR, PARSEZAP, "Failed to add service \"%s\", possible reason already in database?\n", name);
    }
    free(name);
    return 0;
}


