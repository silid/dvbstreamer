#ifndef _DVB_H
#define _DVB_H
#include <linux/types.h>
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
}DVBAdapter_t;

DVBAdapter_t *DVBInit(int adapter);
void DVBDispose(DVBAdapter_t *adapter);
int DVBFrontEndTune(DVBAdapter_t *adapter, struct dvb_frontend_parameters *frontend);
int DVBDemuxStreamEntireTSToDVR(DVBAdapter_t *adapter);
int DVBDemuxSetPESFilter(DVBAdapter_t *adapter, ushort pid, int pidtype, int taptype);
int DVBDVRRead(DVBAdapter_t *adapter, char *data, int max, int timeout);
#endif
