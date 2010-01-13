/*
Copyright (C) 2010  Adam Charrett

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
#include "yamlutils.h"
/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/

typedef struct
{
    char *name;
    char *value;
}
Param;

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/

static char *find_param(const Param *list, const char *name);
static int findMultiplex(DVBDeliverySystem_e delSys, char *freq, char *polarisation, char *satelliteNumber, Multiplex_t **mux);
static int parsezapline(char * str, DVBDeliverySystem_e delSys);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/

static const Param inversion_list [] =
    {
        { "INVERSION_OFF", "OFF" },
        { "INVERSION_ON", "ON" },
        { "INVERSION_AUTO", "AUTO" },
        { NULL, 0 }
    };

static const Param bw_list [] =
    {
        { "BANDWIDTH_6_MHZ", "6000000" },
        { "BANDWIDTH_7_MHZ", "7000000" },
        { "BANDWIDTH_8_MHZ", "8000000" },
        { "BANDWIDTH_AUTO", "AUTO" },
        { NULL, 0 }
    };

static const Param fec_list [] =
    {
        { "FEC_AUTO", "AUTO" },
        { "FEC_1_2", "1/2" },
        { "FEC_2_3", "2/3" },
        { "FEC_3_4", "3/4" },
        { "FEC_4_5", "4/5" },
        { "FEC_5_6", "5/6" },
        { "FEC_6_7", "6/7" },
        { "FEC_7_8", "7/8" },
        { "FEC_8_9", "8/9" },
        { "FEC_NONE", "NONE" },
        { NULL, 0 }
    };

static const Param guard_list [] =
    {
        {"GUARD_INTERVAL_1_16", "1/16" },
        {"GUARD_INTERVAL_1_32", "1/32" },
        {"GUARD_INTERVAL_1_4", "1/4" },
        {"GUARD_INTERVAL_1_8", "1/8" },
        {"GUARD_INTERVAL_AUTO", "AUTO" },
        { NULL, 0 }
    };

static const Param hierarchy_list [] =
    {
        { "HIERARCHY_NONE", "NONE" },
        { "HIERARCHY_1", "1" },
        { "HIERARCHY_2", "2" },
        { "HIERARCHY_4", "4" },
        { "HIERARCHY_AUTO", "AUTO" },            
        { NULL, 0 }
    };

static const Param modulation_list [] =
    {
        { "QPSK", "QPSK" },
        { "QAM_16", "16QAM" },
        { "QAM_32", "32QAM" },
        { "QAM_64", "64QAM" },
        { "QAM_128", "128QAM" },
        { "QAM_256", "256QAM" },
        { "QAM_AUTO", "AUTO QAM" },
#if defined(ENABLE_ATSC)            
        { "8VSB", "8VSB" },
        { "16VSB", "16VSB" },
#endif            
        { NULL, 0 }
    };

static const Param transmissionmode_list [] =
    {
        { "TRANSMISSION_MODE_2K", "2000" },
        { "TRANSMISSION_MODE_8K", "8000" },
        { "TRANSMISSION_MODE_AUTO", "AUTO" },
        { NULL, 0 }
    };
static const char PARSEZAP[] = "ParseZap";

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
int parsezapfile(char *path, DVBDeliverySystem_e delSys)
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
        result = parsezapline(str, delSys);
    }

    return 1;
}

/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/

static char * find_param(const Param *list, const char *name)
{
    while (list->name && strcmp(list->name, name))
    {
        list++;
    }
    return list->value;;
}

static int findMultiplex(DVBDeliverySystem_e delSys, char *freq, char *polarisation, char *satelliteNumber, Multiplex_t **mux)
{
    bool notFound = TRUE;
    MultiplexList_t *muxes = MultiplexGetAll();
    int i;

    Multiplex_t *multiplex;
    for (i = 0; (i < muxes->nrofMultiplexes) && notFound; i ++)
    {
        multiplex = muxes->multiplexes[i];
        if (multiplex)
        {
            yaml_document_t document;
            yaml_node_t *root;
            yaml_node_t *node;
            memset(&document, 0, sizeof(document));
            YamlUtils_Parse(multiplex->tuningParams,&document);
            root = yaml_document_get_root_node(&document);
            node = YamlUtils_MappingFind(&document, root, "Frequency");
            if (node && (node->type == YAML_SCALAR_NODE) && (strcmp((char*)node->data.scalar.value, freq) == 0))
            {
                if ((delSys == DELSYS_DVBS) || (delSys == DELSYS_DVBS2))
                {
                    node = YamlUtils_MappingFind(&document, root, "Polarisation");
                    if (node && (node->type == YAML_SCALAR_NODE) && (strcmp((char*)node->data.scalar.value, polarisation) == 0))
                    {
                        node = YamlUtils_MappingFind(&document, root, "Satellite Number");
                        if (node && (node->type == YAML_SCALAR_NODE) && (strcmp((char*)node->data.scalar.value, satelliteNumber) == 0))
                        {
                            *mux = multiplex;
                            MultiplexRefInc(multiplex);
                            notFound = FALSE;
                        }
                        
                    }
                }
                else
                {
                    *mux = multiplex;
                    MultiplexRefInc(multiplex);
                    notFound = FALSE;
                }
            }
            yaml_document_delete(&document);
        }
    }
    ObjectRefDec(muxes);
    return notFound;
}

static int parsezapline(char * str, DVBDeliverySystem_e delSys)
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
    char params[256];
    char frequency[16] = {0};
    char *polarisation = NULL;
    char satelliteNumber[4] = {0};
    unsigned long freq;
    char *field, *tmp;
    char *name;
    int id;
    int source;
    Multiplex_t *mux;

    tmp = str;
    params[0] = 0;
#define NEXTFIELD() if(!(field = strsep(&tmp, ":")))return -1
#define SETFREQ(_freq) \
    do{\
        sprintf(params, "Frequency: %lu\n", _freq);\
        sprintf(frequency, "%lu", _freq);\
    }while(0)

#define PARAMADD(p...) sprintf(params + strlen(params), p)

    /* find the channel name */
    NEXTFIELD();
    name = strdup(field);

    /* find the frequency */
    NEXTFIELD();
    freq = strtoul(field,NULL,0);


    switch(delSys)
    {
        case DELSYS_DVBS:
            SETFREQ(freq * 1000);
            PARAMADD("Inversion: AUTO\n");
            /* find out the polarisation */
            NEXTFIELD();
            polarisation = (field[0] == 'h' ? "Horizontal":"Vertical");
            PARAMADD("Polarisation: %s\n", polarisation); 
            /* satellite number */
            NEXTFIELD();
            strncpy(satelliteNumber, field, sizeof(satelliteNumber) - 1);
            satelliteNumber[sizeof(satelliteNumber) - 1] = 0;
            PARAMADD("Satellite: %lu\n",  strtoul(field, NULL, 0));
            /* symbol rate */
            NEXTFIELD();
            PARAMADD("Symbol Rate: %lu\n", strtoul(field, NULL, 0) * 1000);
            PARAMADD("FEC: AUTO\n");
        break;
        case DELSYS_DVBC:
            SETFREQ(freq);
            /* find out the inversion */
            NEXTFIELD();
            PARAMADD("Inversion: %s\n", find_param(inversion_list, field));

            /* find out the symbol rate */
            NEXTFIELD();
            PARAMADD("Symbol Rate: %lu\n",strtoul(field, NULL, 0));

            /* find out the fec */
            NEXTFIELD();
            PARAMADD("FEC: %s\n", find_param(fec_list, field));

            /* find out the qam */
            NEXTFIELD();
            PARAMADD("Modulation: %s\n", find_param(modulation_list, field));
        break;
        case DELSYS_DVBT:
            /* DVB-T frequency is in kHz - workaround broken channels.confs */
            if (freq < 1000000)
            {
                freq*=1000;
            }
            SETFREQ(freq);

            /* find out the inversion */
            NEXTFIELD();
            PARAMADD("Inversion: %s\n", find_param(inversion_list, field));

            /* find out the bandwidth */
            NEXTFIELD();
            PARAMADD("Bandwidth: %s\n", find_param(bw_list, field));

            /* find out the fec_hp */
            NEXTFIELD();
            PARAMADD("FEC HP: %s\n", find_param(fec_list, field));

            /* find out the fec_lp */
            NEXTFIELD();
            PARAMADD("FEC LP: %s\n", find_param(fec_list, field));

            /* find out the qam */
            NEXTFIELD();
            PARAMADD("Modulation: %s\n", find_param(modulation_list, field));

            /* find out the transmission mode */
            NEXTFIELD();
            PARAMADD("Transmission Mode: %s\n", find_param(transmissionmode_list, field));

            /* guard list */
            NEXTFIELD();
            PARAMADD("Guard Interval: %s\n", find_param(guard_list, field));

            NEXTFIELD();
            PARAMADD("Hierarchy: %s\n", find_param(hierarchy_list, field));
            break;
#if defined(ENABLE_ATSC)            
        case DELSYS_ATSC:
            SETFREQ(freq);
            PARAMADD("Inversion: AUTO\n");
            NEXTFIELD();
            PARAMADD("Modulation: %s\n", find_param(modulation_list, field));
            break;
#endif            
        default:
            break;
    }

    if (findMultiplex(delSys, frequency, polarisation, satelliteNumber, &mux))
    {
        LogModule(LOG_DEBUGV, PARSEZAP, "Adding frequency %d (delivery system %d)\n", freq, delSys);
        MultiplexAdd(delSys, params, &mux);
    }

    /* Video PID - not used */
    NEXTFIELD();
    /* Audio PID - not used */
    NEXTFIELD();
    /* service ID */
    NEXTFIELD();
    id = strtoul(field, NULL, 0);
    if (delSys == DELSYS_ATSC)
    {
        source = -1;
    }
    else
    {
        source = id;
    }
    LogModule(LOG_DEBUGV, PARSEZAP, "Adding service \"%s\" %d\n", name, id);
    if (ServiceAdd(mux->uid, name, id, source, FALSE, ServiceType_Unknown, -1, 0x1fff, -1))
    {
        LogModule(LOG_ERROR, PARSEZAP, "Failed to add service \"%s\", possible reason already in database?\n", name);
    }
    MultiplexRefDec(mux);
    free(name);
    return 0;
}


