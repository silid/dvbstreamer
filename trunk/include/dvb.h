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
 
dvb.h
 
Opens/Closes and setups dvb adapter for use in the rest of the application.
 
*/
#ifndef _DVB_H
#define _DVB_H
#include <sys/types.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>

typedef struct DVBAdapter_t
{
    int adapter;
    // /dev/dvb/adapter#/frontend0
    char frontendPath[30];
    int frontendfd;
    // /dev/dvb/adapter#/demux0
    char demuxPath[30];
    int demuxfd;
    // /dev/dvb/adapter#/dvr0
    char dvrPath[30];
    int dvrfd;
}
DVBAdapter_t;

DVBAdapter_t *DVBInit(int adapter);
void DVBDispose(DVBAdapter_t *adapter);
int DVBFrontEndTune(DVBAdapter_t *adapter, struct dvb_frontend_parameters *frontend);
int DVBDemuxStreamEntireTSToDVR(DVBAdapter_t *adapter);
int DVBDemuxSetPESFilter(DVBAdapter_t *adapter, ushort pid, int pidtype, int taptype);
int DVBDVRRead(DVBAdapter_t *adapter, char *data, int max, int timeout);
#endif
