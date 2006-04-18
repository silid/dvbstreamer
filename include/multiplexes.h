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

multiplexes.h

Manage multiplexes and tuning parameters.

*/
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
