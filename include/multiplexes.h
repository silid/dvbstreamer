#ifndef _MULTIPLEX_H_
#define _MULTIPLEX_H_
#include <linux/types.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>

typedef struct Multiplex_t
{
	int freq;
	int tsid;
	fe_type_t type;
	int patversion;
}Multiplex_t;

typedef void * MultiplexEnumerator_t;

#define MultiplexAreEqual(_multiplex1, _multiplex2) \
	((_multiplex1)->freq == (_multiplex2)->freq)

int MultiplexCount();
Multiplex_t *MultiplexFind(int freq);
MultiplexEnumerator_t MultiplexEnumeratorGet();
void MultiplexEnumeratorDestroy(MultiplexEnumerator_t enumerator);
Multiplex_t *MultiplexGetNext(MultiplexEnumerator_t enumerator);
int MultiplexFrontendParametersGet(Multiplex_t *multiplex, struct dvb_frontend_parameters *feparams);
int MultiplexAdd(fe_type_t type, struct dvb_frontend_parameters *feparams);
int MultiplexPATVersionSet(Multiplex_t *multiplex, int patversion);
int MultiplexTSIdSet(Multiplex_t *multiplex, int tsid);
int MultiplexNameSet(Multiplex_t *multiplex, char *name);
#endif
