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

tuning.c

Control tuning of the dvb adapter.

*/
#include "config.h"
#include <stdio.h>
#include "main.h"
#include "tuning.h"
#include "cache.h"
#include "logging.h"
#include "dvbadapter.h"
#include "ts.h"
#include "servicefilter.h"
#include "events.h"

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void TuneMultiplex(Multiplex_t *multiplex);
static char *MultiplexChangedEventToString(Event_t event,void * payload);
static char *ServiceChangedEventToString(Event_t event,void * payload);
/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static Multiplex_t *CurrentMultiplex = NULL;
static Service_t *CurrentService = NULL;

static EventSource_t tuningSource;
static Event_t serviceChangedEvent;
static Event_t mulitplexChangedEvent;

static const char TUNING[] = "tuning";

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/

int TuningInit(void)
{
    tuningSource = EventsRegisterSource("Tuning");
    serviceChangedEvent = EventsRegisterEvent(tuningSource, "ServiceChanged", ServiceChangedEventToString);
    mulitplexChangedEvent = EventsRegisterEvent(tuningSource, "MultiplexChanged", MultiplexChangedEventToString);
    return 0;
}

int TuningDeInit(void)
{
    MultiplexRefDec(CurrentMultiplex);
    ServiceRefDec(CurrentService);
    EventsUnregisterSource(tuningSource);
    return 0;
}


/*******************************************************************************
* Channel Change functions                                                     *
*******************************************************************************/
Service_t *TuningCurrentServiceGet(void)
{
    ServiceRefInc(CurrentService);
    return CurrentService;
}

void TuningCurrentServiceSet(Service_t *service)
{
    Multiplex_t *multiplex;
    TSReader_t *reader = MainTSReaderGet();
    ServiceFilter_t primaryServiceFilter;

    if (!service)
    {
        return;
    }

    if ((CurrentService == NULL) || (!ServiceAreEqual(service,CurrentService)))
    {
        LogModule(LOG_DEBUGV, TUNING, "Disabling filters\n");
        TSReaderEnable(reader, FALSE);

        multiplex = MultiplexFindUID(service->multiplexUID);
        primaryServiceFilter = ServiceFilterFindFilter(PrimaryService);

        if ((CurrentMultiplex!= NULL) && MultiplexAreEqual(multiplex, CurrentMultiplex))
        {
            LogModule(LOG_DEBUGV, TUNING, "Same multiplex\n");
            /* TODO Reset primary service filter stats */
        }
        else
        {
            LogModule(LOG_DEBUG, TUNING, "New Multiplex UID = %d (%04x.%04x)\n", multiplex->uid,
                multiplex->networkId & 0xffff, multiplex->tsId & 0xffff);

            TuneMultiplex(multiplex);
            /* Reset all stats as this is a new TS */
            TSReaderZeroStats(reader);
        }

        MultiplexRefDec(multiplex);

        if (CurrentService)
        {
            ServiceRefDec(CurrentService);
        }

        CurrentService = CacheServiceFindId(service->id);
        ServiceFilterServiceSet(primaryServiceFilter, CurrentService);


        /*
         * Inform any interested parties that we have now changed the current
         * service.
         */
        EventsFireEventListeners(serviceChangedEvent, CurrentService);
        LogModule(LOG_DEBUGV, TUNING, "Enabling filters\n");
        TSReaderEnable(reader, TRUE);
    }
}

Multiplex_t *TuningCurrentMultiplexGet(void)
{
    MultiplexRefInc(CurrentMultiplex);
    return CurrentMultiplex;
}

void TuningCurrentMultiplexSet(Multiplex_t *multiplex)
{
    TSReader_t *reader = MainTSReaderGet();
    ServiceFilter_t primaryServiceFilter;

    TSReaderLock(reader);
    LogModule(LOG_DEBUG, TUNING, "Writing changes back to database.\n");
    CacheWriteback();
    TSReaderUnLock(reader);

    LogModule(LOG_DEBUGV, TUNING, "Disabling filters\n");
    TSReaderEnable(reader, FALSE);

    primaryServiceFilter = ServiceFilterFindFilter(PrimaryService);
    ServiceFilterServiceSet(primaryServiceFilter, NULL);

    TuneMultiplex(multiplex);

    TSReaderZeroStats(reader);

    /*
     * Inform any interested parties that we have now changed the current
     * service.
     */
    EventsFireEventListeners(serviceChangedEvent, NULL);

    LogModule(LOG_DEBUGV, TUNING, "Enabling filters\n");
    TSReaderEnable(reader, TRUE);
}

/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/
static void TuneMultiplex(Multiplex_t *multiplex)
{
    struct dvb_frontend_parameters feparams;
    DVBDiSEqCSettings_t diseqc;
    DVBAdapter_t *dvbAdapter = MainDVBAdapterGet();
    TSReader_t *reader = MainTSReaderGet();

    MultiplexRefDec(CurrentMultiplex);

    LogModule(LOG_DEBUGV, TUNING, "Caching Services\n");
    CacheLoad(multiplex);

    MultiplexRefInc(multiplex);
    CurrentMultiplex = multiplex;

    LogModule(LOG_DEBUGV, TUNING, "Getting Frondend parameters\n");
    MultiplexFrontendParametersGet((Multiplex_t*)CurrentMultiplex, &feparams, &diseqc);

    LogModule(LOG_DEBUGV, TUNING, "Tuning\n");
    if (DVBFrontEndTune(dvbAdapter, &feparams, &diseqc))
    {
        LogModule(LOG_ERROR, TUNING, "Tuning failed!\n");
    }

    LogModule(LOG_DEBUGV,TUNING, "Informing TSReader multiplex has changed!\n");
    TSReaderMultiplexChanged(reader, CurrentMultiplex);

    EventsFireEventListeners(mulitplexChangedEvent, multiplex);
}

static char *MultiplexChangedEventToString(Event_t event,void * payload)
{
    char *result=NULL;
    Multiplex_t *mux = payload;
    if (asprintf(&result, "%d", mux->uid) == -1)
    {
        LogModule(LOG_INFO, TUNING, "Failed to allocate memory for multiplex changed event description string.\n");
    }
    return result;
}

static char *ServiceChangedEventToString(Event_t event,void * payload)
{
    char *result=NULL;
    Service_t *service = payload;
    if (asprintf(&result, "%d %04x %s",service->multiplexUID, service->id, service->name) == -1)
    {
        LogModule(LOG_INFO, TUNING, "Failed to allocate memory for service changed event description string.\n");
    }
    return result;
}
