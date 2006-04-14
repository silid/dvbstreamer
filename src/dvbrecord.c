/*
 * The majority of this code was samelessly copied from the Xine dvb input plugin
 */
/*
 * Copyright (C) 2000-2005 the xine project
 * 
 * This file is part of xine, a free video player.
 * 
 * xine is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * xine is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 *
 * Input plugin for Digital TV (Digital Video Broadcast - DVB) devices,
 * e.g. Hauppauge WinTV Nova supported by DVB drivers from Convergence.
 *
 *
 * MODIFICATION HISTORY
 *
 * Date        Author
 * ----        ------
 *
 * 01-Feb-2005 Pekka J‰‰skel‰inen <poj@iki.fi>
 *
 *             - This history log started.
 *             - Disabled the automatic EPG updater thread until EPG demuxer 
 *               is done (it caused pausing of video stream), now EPG is
 *               updated only on demand when the EPG OSD is displayed and
 *               no data is in cache.
 *             - Tried to stabilize the EPG updater thread.
 *             - Fixed a tuning problem I had with Linux 2.6.11-rc2.
 *             - Now tuning to an erroneus channel shouldn't hang but stop
 *               the playback and output a log describing the error.
 *             - Style cleanups here and there.
 *
 *   
 * TODO/Wishlist: (not in any order)
 * - Parse all Administrative PIDs - NIT,SDT,CAT etc
 * - As per James' suggestion, we need a way for the demuxer
 *   to request PIDs from the input plugin.
 * - Timeshift ability.
 * - Pipe teletext infomation to a named fifo so programs such as
 *   Alevtd can read it.
 * - Allow the user to view one set of PIDs (channel) while
 *   recording another on the same transponder - this will require either remuxing or
 *   perhaps bypassing the TS demuxer completely - we could easily have access to the 
 *   individual audio/video streams via seperate read calls, so send them to the decoders
 *   and save the TS output to disk instead of passing it to the demuxer.
 *   This also gives us full control over the streams being played..hmm..control...
 * - Parse and use full EIT for programming info.
 * - Allow the user to find and tune new stations from within xine, and
 *   do away with the need for dvbscan & channels.conf file.
 * - Enable use of Conditional Access devices for scrambled content.
 * - if multiple cards are available, optionally use these to record/gather si info, 
 *   and leave primary card for viewing.
 * - allow for handing off of EPG data to specialised frontends, instead of displaying via
 *   OSD - this will allow for filtering/searching of epg data - useful for automatic recording :)
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#ifdef __sun
#include <sys/ioccom.h>
#endif
#include <sys/poll.h>
#include <poll.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <ctype.h>
#include <getopt.h>
#include <signal.h>

/* These will eventually be #include <linux/dvb/...> */
/*
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
*/
#include <linux/types.h>
#include "dvb/dmx.h"
#include "dvb/frontend.h" 

#define BUFSIZE 16384

#define READ_BUF_SIZE (200 * 188)

#define NOPID 0xffff

/*
 * define stream types 
 * administrative/system PIDs first 
 */
#define INTERNAL_FILTER 0
#define PATFILTER 1
#define PMTFILTER 2
#define EITFILTER 3
#define PCRFILTER 4
#define VIDFILTER 5
#define AUDFILTER 6
#define AC3FILTER 7
#define TXTFILTER 8

#define MAX_FILTERS 9

#define MAX_AUTOCHANNELS 200

#define MAX_SUBTITLES 4


#define bcdtoint(i) ((((i & 0xf0) >> 4) * 10) + (i & 0x0f))

typedef struct {
  int                            fd_frontend;
  int                            fd_pidfilter[MAX_FILTERS];
  int                            fd_subfilter[MAX_SUBTITLES];

  struct dvb_frontend_info       feinfo;
  
  int							 adapter_num;

  char                           frontend_device[100];
  char                           dvr_device[100];
  char                           demux_device[100];
  
  struct dmx_pes_filter_params   pesFilterParams[MAX_FILTERS];
  struct dmx_pes_filter_params   subFilterParams[MAX_SUBTITLES];
  struct dmx_sct_filter_params	 sectFilterParams[MAX_FILTERS]; 
} tuner_t;


typedef struct {
  char                            *name;
  struct dvb_frontend_parameters   front_param;
  int                              pid[MAX_FILTERS];
  int                              subpid[MAX_SUBTITLES];
  int				               service_id;
  int                              sat_no;
  int                              tone;
  int                              pol;
  int				               pmtpid;
} channel_t;

typedef struct {
  char               *mrl;
  char               *channelsconf;
  
  int                 adapter;
  off_t               curpos;
    
  tuner_t            *tuner;
  channel_t          *channels;
  int                 fd;

/* Is channel tuned in correctly, i.e., can we read program stream? */
  int                 tuned_in;
  int                 num_channels;
  int                 channel;

 
  /* CRC table for PAT rebuilding */
  unsigned long       crc32_table[256];
  
  /* scratch buffer for forward seeking */
  char                seek_buf[BUFSIZE];

  int 		      num_streams_in_this_ts;
  /* number of timedout reads in plugin_read */
  int		      read_failcount;
} dvb_input_t;

typedef struct {
	char *name;
	int value;
} Param;

static const Param inversion_list [] = {
	{ "INVERSION_OFF", INVERSION_OFF },
	{ "INVERSION_ON", INVERSION_ON },
	{ "INVERSION_AUTO", INVERSION_AUTO },
        { NULL, 0 }
};

static const Param bw_list [] = {
	{ "BANDWIDTH_6_MHZ", BANDWIDTH_6_MHZ },
	{ "BANDWIDTH_7_MHZ", BANDWIDTH_7_MHZ },
	{ "BANDWIDTH_8_MHZ", BANDWIDTH_8_MHZ },
        { NULL, 0 }
};

static const Param fec_list [] = {
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

static const Param guard_list [] = {
	{"GUARD_INTERVAL_1_16", GUARD_INTERVAL_1_16},
	{"GUARD_INTERVAL_1_32", GUARD_INTERVAL_1_32},
	{"GUARD_INTERVAL_1_4", GUARD_INTERVAL_1_4},
	{"GUARD_INTERVAL_1_8", GUARD_INTERVAL_1_8},
        { NULL, 0 }
};

static const Param hierarchy_list [] = {
	{ "HIERARCHY_1", HIERARCHY_1 },
	{ "HIERARCHY_2", HIERARCHY_2 },
	{ "HIERARCHY_4", HIERARCHY_4 },
	{ "HIERARCHY_NONE", HIERARCHY_NONE },
        { NULL, 0 }
};

static const Param qam_list [] = {
	{ "QPSK", QPSK },
	{ "QAM_128", QAM_128 },
	{ "QAM_16", QAM_16 },
	{ "QAM_256", QAM_256 },
	{ "QAM_32", QAM_32 },
	{ "QAM_64", QAM_64 },
        { NULL, 0 }
};

static const Param transmissionmode_list [] = {
	{ "TRANSMISSION_MODE_2K", TRANSMISSION_MODE_2K },
	{ "TRANSMISSION_MODE_8K", TRANSMISSION_MODE_8K },
        { NULL, 0 }
};

static int verbosity = 0;
static int quit = 0;

/* Utility Functions */

static void printlog(int level, char *format, ...)
{
    if (level <= verbosity)
    {
        va_list valist;
        char *logline;
        va_start(valist, format);
        vasprintf(&logline, format, valist);
        fprintf(stderr, logline);
        va_end(valist);
        free(logline);
    }
}
static void print_error(const char* estring) {
    printlog(0, "ERROR: %s\n", estring);
}


static void ts_build_crc32_table(dvb_input_t *this) {
  uint32_t  i, j, k;

  for( i = 0 ; i < 256 ; i++ ) {
    k = 0;
    for (j = (i << 24) | 0x800000 ; j != 0x80000000 ; j <<= 1) {
      k = (k << 1) ^ (((k ^ j) & 0x80000000) ? 0x04c11db7 : 0);
    }
    this->crc32_table[i] = k;
  }
}

static uint32_t ts_compute_crc32(dvb_input_t *this, uint8_t *data, 
				       uint32_t length, uint32_t crc32) {
  uint32_t i;

  for(i = 0; i < length; i++) {
    crc32 = (crc32 << 8) ^ this->crc32_table[(crc32 >> 24) ^ data[i]];
  }
  return crc32;
}


static unsigned int getbits(unsigned char *buffer, unsigned int bitpos, unsigned int bitcount)
{
    unsigned int i;
    unsigned int val = 0;

    for (i = bitpos; i < bitcount + bitpos; i++) {
      val = val << 1;
      val = val + ((buffer[i >> 3] & (0x80 >> (i & 7))) ? 1 : 0);
    }
    return val;
}


static int find_descriptor(uint8_t tag, const unsigned char *buf, int descriptors_loop_len, 
                                                const unsigned char **desc, int *desc_len)
{

  while (descriptors_loop_len > 0) {
    unsigned char descriptor_tag = buf[0];
    unsigned char descriptor_len = buf[1] + 2;

    if (!descriptor_len) {
      break;
    }

    if (tag == descriptor_tag) {
      if (desc)
        *desc = buf;

      if (desc_len)
        *desc_len = descriptor_len;
	 return 1;
    }

    buf += descriptor_len;
    descriptors_loop_len -= descriptor_len;
  }
  return 0;
}


static void tuner_dispose(tuner_t * this)
{
    int x;

    if (this->fd_frontend >= 0)
      close(this->fd_frontend);

    /* close all pid filter filedescriptors */
    for (x = 0; x < MAX_FILTERS; x++)
      if (this->fd_pidfilter[x] >= 0)
        close(this->fd_pidfilter[x]);

    /* close all pid filter filedescriptors */
    for (x = 0; x < MAX_SUBTITLES; x++)
      if (this->fd_subfilter[x] >= 0)
        close(this->fd_subfilter[x]);
    
    if(this)
      free(this);
}


static tuner_t *tuner_init(int adapter)
{

    tuner_t *this;
    int x;
    int test_video;
    char *video_device=malloc(200);

    assert(video_device != NULL);
    
    this = (tuner_t *) malloc(sizeof(tuner_t));

    assert(this != NULL);
    
    this->fd_frontend = -1;
    for (x = 0; x < MAX_FILTERS; x++)
      this->fd_pidfilter[x] = 0;

    this->adapter_num = adapter;
    
    snprintf(this->frontend_device,100,"/dev/dvb/adapter%i/frontend0",this->adapter_num);
    snprintf(this->demux_device,100,"/dev/dvb/adapter%i/demux0",this->adapter_num);
    snprintf(this->dvr_device,100,"/dev/dvb/adapter%i/dvr0",this->adapter_num);
    snprintf(video_device,100,"/dev/dvb/adapter%i/video0",this->adapter_num);
    
    if ((this->fd_frontend = open(this->frontend_device, O_RDWR)) < 0) {
      printlog( 1, "FRONTEND DEVICE: %s\n", strerror(errno));
      tuner_dispose(this);
      return NULL;
    }

    if ((ioctl(this->fd_frontend, FE_GET_INFO, &this->feinfo)) < 0) {
      printlog(1, "FE_GET_INFO: %s\n", strerror(errno));
      tuner_dispose(this);
      return NULL;
    }

    for (x = 0; x < MAX_FILTERS; x++) {
      this->fd_pidfilter[x] = open(this->demux_device, O_RDWR);
      if (this->fd_pidfilter[x] < 0) {
        printlog(1, "DEMUX DEVICE PIDfilter: %s\n", strerror(errno));
        tuner_dispose(this);
	return NULL;
      }
   }
    for (x = 0; x < MAX_SUBTITLES; x++) {
      this->fd_subfilter[x] = open(this->demux_device, O_RDWR);
      if (this->fd_subfilter[x] < 0) {
        printlog(1, "DEMUX DEVICE Subtitle filter: %s\n", strerror(errno));
      }
   }

   /* open EIT with NONBLOCK */
   if(fcntl(this->fd_pidfilter[EITFILTER], F_SETFL, O_NONBLOCK)<0)
     printlog(1, "Couldn't set EIT to nonblock: %s\n",strerror(errno));
    /* and the internal filter used for PAT & PMT */
   if(fcntl(this->fd_pidfilter[INTERNAL_FILTER], F_SETFL, O_NONBLOCK)<0)
     printlog(1, "Couldn't set EIT to nonblock: %s\n",strerror(errno));
    /* and the frontend */
    fcntl(this->fd_frontend, F_SETFL, O_NONBLOCK);
   
   printlog(1,"Frontend is <%s> ",this->feinfo.name);
   if(this->feinfo.type==FE_QPSK) printlog(2,"SAT Card\n");
   if(this->feinfo.type==FE_QAM) printlog(2,"CAB Card\n");
   if(this->feinfo.type==FE_OFDM) printlog(2,"TER Card\n");

   if ((test_video=open(video_device, O_RDWR)) < 0) {
       printlog(1,"Card has no hardware decoder\n");
   }else{
       printlog(1,"Card HAS HARDWARE DECODER\n");
       close(test_video);
  }

  free(video_device);
  
  return this;
}


static int dvb_set_pidfilter(dvb_input_t * this, int filter, ushort pid, int pidtype, int taptype)
{
    tuner_t *tuner = this->tuner;

   if(this->channels[this->channel].pid [filter] !=NOPID) {
      ioctl(tuner->fd_pidfilter[filter], DMX_STOP);
    }
   
    this->channels[this->channel].pid [filter] = pid;
    tuner->pesFilterParams[filter].pid = pid;
    tuner->pesFilterParams[filter].input = DMX_IN_FRONTEND;
    tuner->pesFilterParams[filter].output = taptype;
    tuner->pesFilterParams[filter].pes_type = pidtype;
    tuner->pesFilterParams[filter].flags = DMX_IMMEDIATE_START;
    if (ioctl(tuner->fd_pidfilter[filter], DMX_SET_PES_FILTER, &tuner->pesFilterParams[filter]) < 0)
    {
	   printlog(2, "set_pid: %s\n", strerror(errno));
	   return 0;
    }
    return 1;
}


static int dvb_set_sectfilter(dvb_input_t * this, int filter, ushort pid, int pidtype, char table, char mask)
{
    tuner_t *tuner = this->tuner;

    if(this->channels[this->channel].pid [filter] !=NOPID) {
      ioctl(tuner->fd_pidfilter[filter], DMX_STOP);
    }
    
    this->channels[this->channel].pid [filter] = pid;
    tuner->sectFilterParams[filter].pid = pid;
    memset(&tuner->sectFilterParams[filter].filter.filter,0,DMX_FILTER_SIZE);
    memset(&tuner->sectFilterParams[filter].filter.mask,0,DMX_FILTER_SIZE);
    tuner->sectFilterParams[filter].timeout = 0;
    tuner->sectFilterParams[filter].filter.filter[0] = table;
    tuner->sectFilterParams[filter].filter.mask[0] = mask;
    tuner->sectFilterParams[filter].flags = DMX_IMMEDIATE_START;
    if (ioctl(tuner->fd_pidfilter[filter], DMX_SET_FILTER, &tuner->sectFilterParams[filter]) < 0){
	   printlog(2,"set_sectionfilter: %s\n", strerror(errno));
	   return 0;
    }
    return 1;
}


static int find_param(const Param *list, const char *name)
{
  while (list->name && strcmp(list->name, name))
    list++;
  return list->value;;
}

static int extract_channel_from_string(channel_t * channel,char * str,fe_type_t fe_type)
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

	tmp = str;
	
	/* find the channel name */
	if(!(field = strsep(&tmp,":")))return -1;
	channel->name = strdup(field);

	/* find the frequency */
	if(!(field = strsep(&tmp, ":")))return -1;
	freq = strtoul(field,NULL,0);

	switch(fe_type)
	{
		case FE_QPSK:
			if(freq > 11700)
			{
				channel->front_param.frequency = (freq - 10600)*1000;
				channel->tone = 1;
			} else {
				channel->front_param.frequency = (freq - 9750)*1000;
				channel->tone = 0;
			}
			channel->front_param.inversion = INVERSION_AUTO;
	  
			/* find out the polarisation */ 
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->pol = (field[0] == 'h' ? 0 : 1);

			/* satellite number */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->sat_no = strtoul(field, NULL, 0);

			/* symbol rate */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.u.qpsk.symbol_rate = strtoul(field, NULL, 0) * 1000;

			channel->front_param.u.qpsk.fec_inner = FEC_AUTO;
		break;
		case FE_QAM:
			channel->front_param.frequency = freq;
			
			/* find out the inversion */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.inversion = find_param(inversion_list, field);

			/* find out the symbol rate */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.u.qam.symbol_rate = strtoul(field, NULL, 0);

			/* find out the fec */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.u.qam.fec_inner = find_param(fec_list, field);

			/* find out the qam */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.u.qam.modulation = find_param(qam_list, field);
		break;
		case FE_OFDM:
  		        /* DVB-T frequency is in kHz - workaround broken channels.confs */
  		        if (freq < 1000000) 
  		          freq*=1000;
		        
		        channel->front_param.frequency = freq;

			/* find out the inversion */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.inversion = find_param(inversion_list, field);

			/* find out the bandwidth */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.u.ofdm.bandwidth = find_param(bw_list, field);

			/* find out the fec_hp */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.u.ofdm.code_rate_HP = find_param(fec_list, field);

			/* find out the fec_lp */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.u.ofdm.code_rate_LP = find_param(fec_list, field);

			/* find out the qam */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.u.ofdm.constellation = find_param(qam_list, field);

			/* find out the transmission mode */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.u.ofdm.transmission_mode = find_param(transmissionmode_list, field);

			/* guard list */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.u.ofdm.guard_interval = find_param(guard_list, field);

			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.u.ofdm.hierarchy_information = find_param(hierarchy_list, field);
		break;
	}

   /* Video PID - not used but we'll take it anyway */
    if (!(field = strsep(&tmp, ":")))
        return -1;
    channel->pid[VIDFILTER] = strtoul(field, NULL, 0);

    /* Audio PID - it's only for mpegaudio so we don't use it anymore */
    if (!(field = strsep(&tmp, ":")))
        return -1;
    channel->pid[AUDFILTER] = strtoul(field, NULL, 0);

    /* service ID */
    if (!(field = strsep(&tmp, ":")))
        return -1;
    channel->service_id = strtoul(field, NULL, 0);

    /* some channel.conf files are generated with the service ID 1 to the right
       this needs investigation */
    if ((field = strsep(&tmp, ":")))
      if(strtoul(field,NULL,0)>0)  
        channel->service_id = strtoul(field, NULL, 0);
        
	return 0;
}

static channel_t *load_channels(dvb_input_t *this, int *num_ch, fe_type_t fe_type) {

  FILE      *f;
  char       str[BUFSIZE];
  channel_t *channels;
  int        num_channels;
  int        i;
 
  f = fopen(this->channelsconf, "rb");
  if (!f) {
     printlog( 0,"Failed to open dvb channel file '%s'\n", this->channelsconf); 
    return NULL;
  }

  /*
   * count and alloc channels
   */
  num_channels = 0;
  while ( fgets (str, BUFSIZE, f)) {
    num_channels++;
  }
  fclose (f);

  if(num_channels > 0) 
    printlog(1, "Expecting %d channels...\n", num_channels);
  else {
    printlog(0,"No channels found in the file: giving up.\n");
    return NULL;
  }

  channels =  malloc (sizeof (channel_t) * num_channels);

   assert(channels != NULL);

  /*
   * load channel list 
   */

  f = fopen (this->channelsconf, "rb");
  num_channels = 0;
  while ( fgets (str, BUFSIZE, f)) {
    if (extract_channel_from_string(&(channels[num_channels]),str,fe_type) < 0) 
	continue;

    num_channels++;
  }

  if(num_channels > 0) 
    printlog(1,"Found %d channels...\n", num_channels);
  else {
    printlog(0,"No channels found in the file: giving up.\n");
    free(channels);
    return NULL;
  }
  
  *num_ch = num_channels;
  return channels;
}

static int tuner_set_diseqc(tuner_t *this, channel_t *c)
{
   struct dvb_diseqc_master_cmd cmd =
      {{0xe0, 0x10, 0x38, 0xf0, 0x00, 0x00}, 4};

   cmd.msg[3] = 0xf0 | ((c->sat_no * 4) & 0x0f) |
      (c->tone ? 1 : 0) | (c->pol ? 0 : 2);

   if (ioctl(this->fd_frontend, FE_SET_TONE, SEC_TONE_OFF) < 0)
      return 0;
   if (ioctl(this->fd_frontend, FE_SET_VOLTAGE,
	     c->pol ? SEC_VOLTAGE_13 : SEC_VOLTAGE_18) < 0)
      return 0;
   usleep(15000);
   if (ioctl(this->fd_frontend, FE_DISEQC_SEND_MASTER_CMD, &cmd) < 0)
      return 0;
   usleep(15000);
   if (ioctl(this->fd_frontend, FE_DISEQC_SEND_BURST,
	     (c->sat_no / 4) % 2 ? SEC_MINI_B : SEC_MINI_A) < 0)
      return 0;
   usleep(15000);
   if (ioctl(this->fd_frontend, FE_SET_TONE,
	     c->tone ? SEC_TONE_ON : SEC_TONE_OFF) < 0)
      return 0;

   return 1;
}


/* Tune to the requested freq. etc, wait for frontend to lock for a few seconds.
 * if frontend can't lock, retire. */
static int tuner_tune_it (tuner_t *this, struct dvb_frontend_parameters
			  *front_param) {
  fe_status_t status = 0;
/*  fe_status_t festatus; */
  struct dvb_frontend_event event;
  unsigned int strength;
  struct pollfd pfd[1];

  /* discard stale events */
  while (ioctl(this->fd_frontend, FE_GET_EVENT, &event) != -1);

  if (ioctl(this->fd_frontend, FE_SET_FRONTEND, front_param) <0) {
    printlog(1,"setfront front: %s\n", strerror(errno));
    return 0;
  }

  pfd[0].fd = this->fd_frontend;
  pfd[0].events = POLLIN;

  if (poll(pfd,1,3000)){
      if (pfd[0].revents & POLLIN){
	  if (ioctl(this->fd_frontend, FE_GET_EVENT, &event) == -EOVERFLOW){
	      print_error("EOVERFLOW");
	      return 0;
	  }
	  if (event.parameters.frequency <= 0)
	      return 0;
      }
  }
  
  do {
    status = 0;
    if (ioctl(this->fd_frontend, FE_READ_STATUS, &status) < 0) {
      printlog(1,"fe get event: %s\n", strerror(errno));
      return 0;
    }

    printlog(2,"status: %x\n", status);
    if (status & FE_HAS_LOCK) {
      break;
    }
    usleep(500000);
    print_error("Trying to get lock...");
  } while (!(status & FE_TIMEDOUT));
  
  /* inform the user of frontend status */ 
  printlog(2,"Tuner status:  ");
/*  if (ioctl(this->fd_frontend, FE_READ_STATUS, &status) >= 0){ */
    if (status & FE_HAS_SIGNAL) 
	printlog(2," FE_HAS_SIGNAL");
    if (status & FE_TIMEDOUT) 
	printlog(2," FE_TIMEDOUT");
    if (status & FE_HAS_LOCK) 
	printlog(2," FE_HAS_LOCK");
    if (status & FE_HAS_CARRIER) 
	printlog(2," FE_HAS_CARRIER");
    if (status & FE_HAS_VITERBI) 
	printlog(2," FE_HAS_VITERBI");
    if (status & FE_HAS_SYNC) 
	printlog(2," FE_HAS_SYNC");
/*  } */
  printf("\n");
  
  strength=0;
  if(ioctl(this->fd_frontend,FE_READ_BER,&strength) >= 0)
    printlog(2," Bit error rate: %i\n",strength);

  strength=0;
  if(ioctl(this->fd_frontend,FE_READ_SIGNAL_STRENGTH,&strength) >= 0)
    printlog(2," Signal strength: %i\n",strength);

  strength=0;
  if(ioctl(this->fd_frontend,FE_READ_SNR,&strength) >= 0)
    printlog(2," Signal/Noise Ratio: %i\n",strength);
 
  if (status & FE_HAS_LOCK && !(status & FE_TIMEDOUT)) {
    printlog(2," Lock achieved at %lu Hz\n",(unsigned long)front_param->frequency);   
    return 1;
  } else {
    printlog(0,"Unable to achieve lock at %lu Hz\n",(unsigned long)front_param->frequency);
    return 0;
  }

}

/* Parse the PMT, and add filters for all stream types associated with 
 * the 'channel'. We leave it to the demuxer to sort out which PIDs to 
 * use. to simplify things slightly, (and because the demuxer can't handle it)
 * allow only one of each media type */
static void parse_pmt(dvb_input_t *this, const unsigned char *buf, int section_length)
{
 
  int program_info_len;
  int pcr_pid;
  int has_video=0;
  int has_audio=0;
  int has_ac3=0;
  int has_subs=0;
  int has_text=0;

  dvb_set_pidfilter(this, PMTFILTER, this->channels[this->channel].pmtpid, DMX_PES_OTHER,DMX_OUT_TS_TAP);
  dvb_set_pidfilter(this, PATFILTER, 0, DMX_PES_OTHER,DMX_OUT_TS_TAP);

  pcr_pid = ((buf[0] & 0x1f) << 8) | buf[1];
  if(pcr_pid!=0x1FFF) /* don't waste time if the PCR is invalid */
  {
    printlog(2," Adding PCR     : PID 0x%04x\n", pcr_pid);
    dvb_set_pidfilter(this, PCRFILTER, pcr_pid, DMX_PES_PCR,DMX_OUT_TS_TAP);
   }

  program_info_len = ((buf[2] & 0x0f) << 8) | buf[3];
  buf += program_info_len + 4;
  section_length -= program_info_len + 4;

  while (section_length >= 5) {
    int elementary_pid = ((buf[1] & 0x1f) << 8) | buf[2];
    int descriptor_len = ((buf[3] & 0x0f) << 8) | buf[4];
    switch (buf[0]) {
      case 0x01:
      case 0x02:
        if(!has_video) {
          printlog(2," Adding VIDEO     : PID 0x%04x\n", elementary_pid);
	  dvb_set_pidfilter(this, VIDFILTER, elementary_pid, DMX_PES_VIDEO, DMX_OUT_TS_TAP);
	  has_video=1;
	} 
	break;
	
      case 0x03:
      case 0x04:
        if(!has_audio) {
	  printlog(2," Adding AUDIO     : PID 0x%04x\n", elementary_pid);
	  dvb_set_pidfilter(this, AUDFILTER, elementary_pid, DMX_PES_AUDIO, DMX_OUT_TS_TAP);
	  has_audio=1;
	}
        break;
        
      case 0x06:
        if (find_descriptor(0x56, buf + 5, descriptor_len, NULL, NULL)) {
	  if(!has_text) {
	     printlog(2," Adding TELETEXT  : PID 0x%04x\n", elementary_pid);
	     dvb_set_pidfilter(this,TXTFILTER, elementary_pid, DMX_PES_OTHER,DMX_OUT_TS_TAP);
             has_text=1;
          } 
	  break;
	} else if (find_descriptor (0x59, buf + 5, descriptor_len, NULL, NULL)) {
           /* Note: The subtitling descriptor can also signal
	    * teletext subtitling, but then the teletext descriptor
	    * will also be present; so we can be quite confident
	    * that we catch DVB subtitling streams only here, w/o
	    * parsing the descriptor. */
	    if(has_subs <= MAX_SUBTITLES) {
              printlog(2," Adding SUBTITLES: PID 0x%04x\n", elementary_pid);
               if(this->channels[this->channel].subpid [has_subs] !=NOPID) {
                  ioctl(this->tuner->fd_subfilter[has_subs], DMX_STOP);
               }
               this->channels[this->channel].subpid [has_subs] = elementary_pid;
               this->tuner->subFilterParams[has_subs].pid = elementary_pid;
               this->tuner->subFilterParams[has_subs].input = DMX_IN_FRONTEND;
               this->tuner->subFilterParams[has_subs].output = DMX_OUT_TS_TAP;
               this->tuner->subFilterParams[has_subs].pes_type = DMX_PES_OTHER;
               this->tuner->subFilterParams[has_subs].flags = DMX_IMMEDIATE_START;
               if (ioctl(this->tuner->fd_subfilter[has_subs], DMX_SET_PES_FILTER, &this->tuner->subFilterParams[has_subs]) < 0)
               {
               	   printlog(1,"set_pid: %s\n", strerror(errno));
                   break;
               }
               has_subs++;
            }
	    break;
        } else if (find_descriptor (0x6a, buf + 5, descriptor_len, NULL, NULL)) {
            if(!has_ac3) {
 	      dvb_set_pidfilter(this, AC3FILTER, elementary_pid, DMX_PES_OTHER,DMX_OUT_TS_TAP);
              printlog(2," Adding AC3       : PID 0x%04x\n", elementary_pid);
              has_ac3=1;
        }
	break;
	}
      break;
      };

    buf += descriptor_len + 5;
    section_length -= descriptor_len + 5;
  };
}

static void dvb_parse_si(dvb_input_t *this) {

  char *tmpbuffer;
  char *bufptr;
  int 	service_id;
  int	result;
  int  	section_len;
  int 	x;
  struct pollfd pfd;
  
  tuner_t *tuner = this->tuner;
  tmpbuffer = malloc (8192);

  assert(tmpbuffer != NULL);

  bufptr = tmpbuffer;

  pfd.fd=tuner->fd_pidfilter[INTERNAL_FILTER];
  pfd.events = POLLPRI;

  printlog(2,"Setting up Internal PAT filter\n");

   usleep(500000);
   
  /* first - the PAT. retrieve the entire section...*/  
  dvb_set_sectfilter(this, INTERNAL_FILTER, 0, DMX_PES_OTHER, 0, 0xff);

  /* wait for up to 15 seconds */
  if(poll(&pfd,1,12000)<1) /* PAT timed out - weird, but we'll default to using channels.conf info */
  {
    printlog(1,"Error setting up Internal PAT filter - reverting to rc6 hehaviour\n");
    dvb_set_pidfilter (this,VIDFILTER,this->channels[this->channel].pid[VIDFILTER], DMX_PES_OTHER, DMX_OUT_TS_TAP);
    dvb_set_pidfilter (this,AUDFILTER,this->channels[this->channel].pid[AUDFILTER], DMX_PES_OTHER, DMX_OUT_TS_TAP);
    return;
  }
  result = read (tuner->fd_pidfilter[INTERNAL_FILTER], tmpbuffer, 3);
    
  if(result!=3)
    printlog(1,"Error reading PAT table - no data!\n");

  section_len = getbits(tmpbuffer,12,12);
  result = read (tuner->fd_pidfilter[INTERNAL_FILTER], tmpbuffer+5,section_len);
  
  if(result!=section_len)
    printlog(1,"Error reading in the PAT table\n");

  ioctl(tuner->fd_pidfilter[INTERNAL_FILTER], DMX_STOP);

  bufptr+=10;
  this->num_streams_in_this_ts=0;
  section_len-=5;    

  while(section_len>4){
    service_id = getbits (bufptr,0,16);
    for (x=0;x<this->num_channels;x++){
      if(this->channels[x].service_id==service_id) {
        this->channels[x].pmtpid = getbits (bufptr, 19, 13);
      }
    }
    section_len-=4;
    bufptr+=4;
    if(service_id>0) /* ignore NIT table for now */
      this->num_streams_in_this_ts++;        
  }

  bufptr = tmpbuffer;

    /* next - the PMT */
  printlog(2,"Setting up Internal PMT filter for pid %x\n",this->channels[this->channel].pmtpid);

  dvb_set_sectfilter(this, INTERNAL_FILTER, this->channels[this->channel].pmtpid, DMX_PES_OTHER, 2, 0xff);

  if((poll(&pfd,1,15000)<1) || this->channels[this->channel].pmtpid==0) /* PMT timed out or couldn't be found - default to using channels.conf info */
  {
    printlog(0,"WARNING **** Reverting to rc6 hehaviour. Please regenerate your channels.conf in ?zap format ****\n");
    dvb_set_pidfilter (this,VIDFILTER,this->channels[this->channel].pid[VIDFILTER], DMX_PES_OTHER, DMX_OUT_TS_TAP);
    dvb_set_pidfilter (this,AUDFILTER,this->channels[this->channel].pid[AUDFILTER], DMX_PES_OTHER, DMX_OUT_TS_TAP);
    return;
  }
  result = read(tuner->fd_pidfilter[INTERNAL_FILTER],tmpbuffer,3);

  section_len = getbits (bufptr, 12, 12);
  result = read(tuner->fd_pidfilter[INTERNAL_FILTER],tmpbuffer+3,section_len);

  ioctl(tuner->fd_pidfilter[INTERNAL_FILTER], DMX_STOP);

  parse_pmt(this,tmpbuffer+8,section_len);
  
/*
  dvb_set_pidfilter(this, TSDTFILTER, 0x02,DMX_PES_OTHER,DMX_OUT_TS_TAP);
  dvb_set_pidfilter(this, RSTFILTER, 0x13,DMX_PES_OTHER,DMX_OUT_TS_TAP);
  dvb_set_pidfilter(this, TDTFILTER, 0x14,DMX_PES_OTHER,DMX_OUT_TS_TAP);
  dvb_set_pidfilter(this, DITFILTER, 0x1e,DMX_PES_OTHER,DMX_OUT_TS_TAP);
  dvb_set_pidfilter(this, CATFILTER, 0x01,DMX_PES_OTHER,DMX_OUT_TS_TAP);
  dvb_set_pidfilter(this, NITFILTER, 0x10,DMX_PES_OTHER,DMX_OUT_TS_TAP);
  dvb_set_pidfilter(this, SDTFILTER, 0x11, DMX_PES_OTHER, DMX_OUT_TS_TAP);
*/

  printlog(2,"Setup of PID filters complete\n");

  free(tmpbuffer);
}

/* Helper function for finding the channel index in the channels struct
   given the service_id. If channel is not found, -1 is returned. */
static int channel_index(dvb_input_t* this, unsigned int service_id) {
  int n;
  for (n=0; n < this->num_channels; n++)
    if (this->channels[n].service_id == service_id) 
	return n;

  return -1;
}


static int tuner_set_channel (dvb_input_t *this, channel_t *c) {
  tuner_t *tuner=this->tuner;

  if (tuner->feinfo.type==FE_QPSK) {
    if(!(tuner->feinfo.caps & FE_CAN_INVERSION_AUTO))
      c->front_param.inversion = INVERSION_OFF;
    if (!tuner_set_diseqc(tuner, c))
      return 0;
  }

  if (!tuner_tune_it (tuner, &c->front_param)){
    return 0;
  }
 
  return 1; /* fixme: error handling */
}

/* parse TS and re-write PAT to contain only our pmt */
static void ts_rewrite_packets (dvb_input_t *this, unsigned char * originalPkt, int len) {

#define PKT_SIZE 188
#define BODY_SIZE (188-4) 
  unsigned int  sync_byte;
  unsigned int  data_offset;
  unsigned int  data_len;
  unsigned int 	pid;

  while(len>0){
  
    sync_byte                      = originalPkt[0];
    pid                            = ((originalPkt[1] << 8) | originalPkt[2]) & 0x1fff;
                                      
    /*
     * Discard packets that are obviously bad.
     */

    data_offset = 4;
    originalPkt+=data_offset;
    
    if (pid == 0 && sync_byte==0x47) {
      unsigned long crc;
      
      originalPkt[3]=13; /* section length including CRC - first 3 bytes */
      originalPkt[2]=0x80;
      originalPkt[7]=0; /* section number */
      originalPkt[8]=0; /* last section number */
      originalPkt[9]=(this->channels[this->channel].service_id >> 8) & 0xff;
      originalPkt[10]=this->channels[this->channel].service_id & 0xff;
      originalPkt[11]=(this->channels[this->channel].pmtpid >> 8) & 0xff;
      originalPkt[12]=this->channels[this->channel].pmtpid & 0xff;

      crc= ts_compute_crc32 (this, originalPkt+1, 12, 0xffffffff);
      
      originalPkt[13]=(crc>>24) & 0xff;
      originalPkt[14]=(crc>>16) & 0xff;
      originalPkt[15]=(crc>>8) & 0xff;
      originalPkt[16]=crc & 0xff;
      memset(originalPkt+17,0xFF,PKT_SIZE-21); /* stuff the remainder */
      
    }

  data_len = PKT_SIZE - data_offset;
  originalPkt+=data_len;
  len-=data_len;

  }
}

static off_t dvb_read (dvb_input_t *this, char *buf, off_t len) {
  off_t n=0, total=0;
  struct pollfd pfd;

  if (!this->tuned_in)
      return 0;

  while (total<len){ 
      pfd.fd = this->fd;
      pfd.events = POLLPRI | POLLIN | POLLERR;
      pfd.revents = 0;
      
      if (!this->tuned_in) {
	  printlog(1,"Channel \"%s\" could not be tuned in. "
		  "Possibly erroneus settings in channels.conf "
		  "(frequency changed?).\n", 
		  this->channels[this->channel].name);
	  return 0;
      }
  
      if (poll(&pfd, 1, 1500) < 1) { 
	  printlog(1,"No data available.  Signal Lost??  \n");
	  this->read_failcount++;
	  break;
      }

      if (this->read_failcount) { 
      /* signal/stream regained after loss - 
	 kick the net_buf_control layer. */
	  this->read_failcount=0;
	  printlog(1, "Data resumed...\n");
      }

      if (pfd.revents & POLLPRI || pfd.revents & POLLIN) {
	  n = read (this->fd, &buf[total], len-total);
      } else {
	  if (pfd.revents & POLLERR) {
	      printlog(1, "No data available.  Signal Lost??  \n"); 
	      this->read_failcount++;
	      break;
	  } 
      	}
      printlog(3, "Got %ld bytes (%ld/%ld bytes read)\n",  n, total,len);
    
      if (n > 0){  
	  this->curpos += n;
	  total += n;
      } else if (n < 0 && errno!=EAGAIN) {
	  break;
      }
	  n = 0;
  }

  ts_rewrite_packets (this, buf,total);
  
  /* no data for several seconds - tell the user a possible reason */
  if(this->read_failcount==5){
   printlog(1,"DVB Signal Lost.  Please check connections."); 
  }
  return total;
}

static void dvb_dispose (dvb_input_t *this) {
  int i, j;

  if (this->fd != -1) {
    close(this->fd);
    this->fd = -1;
  }

  if (this->channels)
    free (this->channels);

  if (this->tuner)
    tuner_dispose (this->tuner);
}

static int dvb_open(dvb_input_t * this)
{
    tuner_t *tuner;
    channel_t *channels;
    int num_channels;
    char str[256];
    char *ptr;
    int x;
    char *channame = this->mrl;

    if (!(tuner = tuner_init(this->adapter)))
    {
        printlog(0,"Cannot open dvb device\n");
        return 0;
    }

    if (!(channels = load_channels(this, &num_channels, tuner->feinfo.type))) 
    {
        /* failed to load the channels */
        tuner_dispose(tuner);
        return 0;
    }

    
	if (*channame) {
	  /* try to find the specified channel */
	  int idx = 0;
	  printlog(1,"Searching for channel %s\n", channame);

	  while (idx < num_channels) {
	    if (strcasecmp(channels[idx].name, channame) == 0)
	      break;
            idx++;
          }

	 if (idx < num_channels) {
	   this->channel = idx;
         } else {
           /*
            * try a partial match too
	    * be smart and compare starting from the first char, then from 
	    * the second etc..
	    * Yes, this is expensive, but it happens really often
	    * that the channels have really ugly names, sometimes prefixed
	    * by numbers...
	    */
	    int chanlen = strlen(channame);
	    int offset = 0;

	    printlog(1,"Exact match for %s not found: trying partial matches\n", channame);

            do {
	      idx = 0;
	      while (idx < num_channels) {
	        if (strlen(channels[idx].name) > offset) {
		  if (strncasecmp(channels[idx].name + offset, channame, chanlen) == 0) {
                     printlog(1,"Found matching channel %s\n", channels[idx].name);
                     break;			  
                  }
		}
		idx++;
              }
	      offset++;
	      printlog(2,"%d,%d,%d\n", offset, idx, num_channels);
            }
            while ((offset < 6) && (idx == num_channels));
              if (idx < num_channels) {
                this->channel = idx;
              } else {
                printlog(1,"Channel %s not found in channels.conf, defaulting.\n", channame);
                this->channel = 0;
              }
            }
	  } else {
		printlog(0,"Failed to find channel!\n");
		 /* not our mrl */
	   tuner_dispose(tuner);
	   return 0;
        }

    this->tuner = tuner;
    this->channels = channels;
    this->num_channels = num_channels;
    
    if (!tuner_set_channel(this, &this->channels[this->channel])) {
      printlog(0,"Tuner_set_channel failed\n");
      return 0;
    }

    if ((this->fd = open(this->tuner->dvr_device, O_RDONLY |O_NONBLOCK)) < 0) {
      printlog(0,"Cannot open dvr device '%s'\n", this->tuner->dvr_device);
      return 0;
    }
    this->tuned_in = 1;
    
    /* now read the pat, find all accociated PIDs and add them to the stream */
    dvb_parse_si(this);

    this->curpos = 0;

    /* compute CRC table for rebuilding pat */
    ts_build_crc32_table(this);

    /* Clear all pids, the pmt will tell us which to use */
    for (x = 0; x < MAX_FILTERS; x++){
      this->channels[this->channel].pid[x] = NOPID;
    }  
    return 1;
}
static void usage(void)
{
    fprintf(stderr,"Usage:dvbrecord <options> channel\n"
                   "      Options:\n"
                   "      -v           : Increase the amount of debug output,\n"
                   "                     can be used multiple times for more output\n"
                   "      -f <file>    : Output transport stream to <file>\n"
                   "      -a <adapter> : Use adapter number\n"
                   "      -c <file>    : channels.conf file to use\n"
                   "                     (default is ~/.dvbrecord/channels.conf\n"
           );
}

static void sighandler(int signal)
{
    quit = 1;
}

int main(int argc, char *argv[])
{
	int i;
    int outfd = 1; /* Default to stdout */
	char * buffer;
	dvb_input_t dvb;
    char channelsFile[256];

    memset(&dvb, 0, sizeof(dvb));
    sprintf(channelsFile,"%s/.dvbrecord/channels.conf", getenv("HOME"));
    dvb.channelsconf = channelsFile;
    signal(SIGINT, sighandler);
    signal(SIGQUIT,sighandler); 
    while (1) {
        char c;
        c = getopt(argc, argv, "vf:a:c:");
        if (c == -1) {
            break;
        }
        switch (c)
        {
            case 'v': verbosity ++;
                      break;
            case 'f': printlog(1,"Output file is now %s\n", optarg);
                      outfd = open(optarg, O_CREAT|O_WRONLY,  S_IRUSR |  S_IWUSR );
                      if (outfd == -1)
                      {
                        printlog(0,"Faild to open %s for writing\n", optarg);
                      }
                      break;
            case 'a': dvb.adapter = atoi(optarg);
                      printlog(1,"Using adapter %d\n", dvb.adapter);
                      break;
            case 'c': strcpy(channelsFile, optarg);
                      printlog(1,"Using channels file %s\n", channelsFile);
                      break;
            default:
                usage();
                exit(1);
        }
    }
    if (optind < argc) {
        dvb.mrl = argv[optind];
    } else {
        usage();
        exit(1);
    }
	
	buffer = malloc(READ_BUF_SIZE);
	if (!dvb_open(&dvb))
	{
       printlog(0,"Initialisation failed!\n");
       exit(1);
	}
	while(!quit)
	{
        off_t bytesread;
		bytesread = dvb_read(&dvb, buffer, READ_BUF_SIZE);
        write(outfd, buffer, bytesread);
	}
	dvb_dispose(&dvb);
	free(buffer);
	return 0;	
}
