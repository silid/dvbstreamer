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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "multiplexes.h"
#include "services.h"
#include "logging.h"

typedef struct
{
    char *name;
    int value;
}
Param;

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
        { NULL, 0 }
    };

static const Param fec_list [] =
    {
        { "FEC_1_2", FEC_1_2 },
        { "FEC_2_3", FEC_2_3 },
        { "FEC_3_4", FEC_3_4 },
        { "FEC_4_5", FEC_4_5 },
        { "FEC_5_6", FEC_5_6 },
        { "FEC_6_7", FEC_6_7 },
        { "FEC_7_8", FEC_7_8 },
        { "FEC_8_9", FEC_8_9 },
        { "FEC_AUTO", FEC_AUTO },
        { "FEC_NONE", FEC_NONE },
        { NULL, 0 }
    };

static const Param guard_list [] =
    {
        {"GUARD_INTERVAL_1_16", GUARD_INTERVAL_1_16},
        {"GUARD_INTERVAL_1_32", GUARD_INTERVAL_1_32},
        {"GUARD_INTERVAL_1_4", GUARD_INTERVAL_1_4},
        {"GUARD_INTERVAL_1_8", GUARD_INTERVAL_1_8},
        { NULL, 0 }
    };

static const Param hierarchy_list [] =
    {
        { "HIERARCHY_1", HIERARCHY_1 },
        { "HIERARCHY_2", HIERARCHY_2 },
        { "HIERARCHY_4", HIERARCHY_4 },
        { "HIERARCHY_NONE", HIERARCHY_NONE },
        { NULL, 0 }
    };

static const Param qam_list [] =
    {
        { "QPSK", QPSK },
        { "QAM_128", QAM_128 },
        { "QAM_16", QAM_16 },
        { "QAM_256", QAM_256 },
        { "QAM_32", QAM_32 },
        { "QAM_64", QAM_64 },
        { NULL, 0 }
    };

static const Param transmissionmode_list [] =
    {
        { "TRANSMISSION_MODE_2K", TRANSMISSION_MODE_2K },
        { "TRANSMISSION_MODE_8K", TRANSMISSION_MODE_8K },
        { NULL, 0 }
    };

static int find_param(const Param *list, const char *name)
{
    while (list->name && strcmp(list->name, name))
        list++;
    return list->value;;
}

static int parsezapline(char * str, fe_type_t fe_type)
{
    /*
		try to extract channel data from a string in the following format
		(DVBS) QPSK: <channel name>:<frequency>:<polarisation>:<sat_no>:<sym_rate>:<vpid>:<apid>
		(DVBC) QAM: <channel name>:<frequency>:<inversion>:<sym_rate>:<fec>:<qam>:<vpid>:<apid>
		(DVBT) OFDM: <channel name>:<frequency>:<inversion>:
						<bw>:<fec_hp>:<fec_lp>:<qam>:
						<transmissionm>:<guardlist>:<hierarchinfo>:<vpid>:<apid>
		
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


    */
    unsigned long freq;
    char *field, *tmp;
    struct dvb_frontend_parameters   front_param;
    char *name;
    int id;

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
			/*
			if(freq > 11700)
			{
				front_param.frequency = (freq - 10600)*1000;
				diseqcsettings->tone = 1;
			} else {
				front_param.frequency = (freq - 9750)*1000;
				diseqcsettings->tone = 0;
			}*/
			front_param.frequency = freq;
			front_param.inversion = INVERSION_AUTO;
	  
			/* find out the polarisation */ 
			NEXTFIELD();
			/*diseqcsettings->pol = (field[0] == 'h' ? 0 : 1);*/

			/* satellite number */
			NEXTFIELD();
			/*diseqcsettings->sat_no = strtoul(field, NULL, 0);*/

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
			front_param.u.qam.modulation = find_param(qam_list, field);
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
		    front_param.u.ofdm.constellation = find_param(qam_list, field);

		    /* find out the transmission mode */
		    NEXTFIELD();
		    front_param.u.ofdm.transmission_mode = find_param(transmissionmode_list, field);

		    /* guard list */
		    NEXTFIELD();
		    front_param.u.ofdm.guard_interval = find_param(guard_list, field);

		    NEXTFIELD();
		    front_param.u.ofdm.hierarchy_information = find_param(hierarchy_list, field);
			break;
		default:
			break;
	}
	
	printlog(LOG_DEBUGV,"Adding frequency %d (type %d)\n", front_param.frequency, fe_type);
	MultiplexAdd(fe_type, &front_param);
	
    /* Video PID - not used but we'll take it anyway */
    NEXTFIELD();
    /* Audio PID - it's only for mpegaudio so we don't use it anymore */
    NEXTFIELD();
    /* service ID */
    NEXTFIELD();
    id = strtoul(field, NULL, 0);
    printlog(LOG_DEBUGV, "Adding service \"%s\" %d\n", name, id);
    ServiceAdd(front_param.frequency, name, id, -1, -1);
    return 0;
}

int parsezapfile(char *path, fe_type_t fe_type)
{
    FILE      *f;
    char       str[255];
    int        result;

    f = fopen(path, "rb");
    if (!f)
    {
        fprintf( stderr, "Failed to open dvb channel file '%s'\n", path);
        return 0;
    }

    while ( fgets (str, sizeof(str), f))
    {
        result = parsezapline(str, fe_type);
    }

    return 1;
}
