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
#include "dvb.h"
#include "ts.h"
#include "servicefilter.h"
#include "events.h"

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void ChannelChangedDoCallbacks(Multiplex_t *multiplex, Service_t *service);
static void TuneMultiplex(Multiplex_t *multiplex);
static char *MultiplexChangedEventToString(Event_t event,void * payload);
static char *ServiceChangedEventToString(Event_t event,void * payload);
/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static Multiplex_t *CurrentMultiplex = NULL;
static Service_t *CurrentService = NULL;
static List_t *ChannelChangedCallbacksList = NULL;

static EventSource_t tuningSource;
static Event_t serviceChangedEvent;
static Event_t mulitplexChangedEvent;

static const char TUNING[] = "tuning";

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/

int TuningInit(void)
{
    ChannelChangedCallbacksList = ListCreate();
    tuningSource = EventsRegisterSource("Tuning");
    serviceChangedEvent = EventsRegisterEvent(tuningSource, "ServiceChanged", ServiceChangedEventToString);
    mulitplexChangedEvent = EventsRegisterEvent(tuningSource, "MultiplexChanged", MultiplexChangedEventToString);
    return 0;
}

int TuningDeInit(void)
{
    ListFree(ChannelChangedCallbacksList,NULL);
    MultiplexRefDec(CurrentMultiplex);
    ServiceRefDec(CurrentService);
    EventsUnregisterSource(tuningSource);
    return 0;
}


/*******************************************************************************
* Channel Change functions                                                     *
*******************************************************************************/
void TuningChannelChangedRegisterCallback(PluginChannelChanged_t callback)
{

    if (ChannelChangedCallbacksList)
    {
        ListAdd(ChannelChangedCallbacksList, callback);
    }
}

void TuningChannelChangedUnRegisterCallback(PluginChannelChanged_t callback)
{
    if (ChannelChangedCallbacksList)
    {
        ListRemove(ChannelChangedCallbacksList, callback);
    }
}

Service_t *TuningCurrentServiceGet(void)
{
    ServiceRefInc(CurrentService);
    return CurrentService;
}

void TuningCurrentServiceSet(Service_t *service)
{
    Multiplex_t *multiplex;
    TSFilter_t *tsFilter = MainTSFilterGet();
    PIDFilter_t *primaryServiceFilter;

    if (!service)
    {
        return;
    }

    if ((CurrentService == NULL) || (!ServiceAreEqual(service,CurrentService)))
    {
        LogModule(LOG_DEBUGV, TUNING, "Disabling filters\n");
        TSFilterEnable(tsFilter, FALSE);

        multiplex = MultiplexFindUID(service->multiplexUID);
        primaryServiceFilter = TSFilterFindPIDFilter(tsFilter, PrimaryService, ServicePIDFilterType);

        if ((CurrentMultiplex!= NULL) && MultiplexAreEqual(multiplex, CurrentMultiplex))
        {
            LogModule(LOG_DEBUGV, TUNING, "Same multiplex\n");
            /* Reset primary service filter stats */
            primaryServiceFilter->packetsFiltered  = 0;
            primaryServiceFilter->packetsProcessed = 0;
            primaryServiceFilter->packetsOutput    = 0;
        }
        else
        {
            LogModule(LOG_DEBUG, TUNING, "New Multiplex UID = %d (%04x.%04x)\n", multiplex->uid,
                multiplex->networkId & 0xffff, multiplex->tsId & 0xffff);

            TuneMultiplex(multiplex);
            /* Reset all stats as this is a new TS */
            TSFilterZeroStats(tsFilter);
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
        ChannelChangedDoCallbacks((Multiplex_t *)CurrentMultiplex, (Service_t *)CurrentService);
        EventsFireEventListeners(serviceChangedEvent, CurrentService);
        LogModule(LOG_DEBUGV, TUNING, "Enabling filters\n");
        TSFilterEnable(tsFilter, TRUE);
    }
}

Multiplex_t *TuningCurrentMultiplexGet(void)
{
    MultiplexRefInc(CurrentMultiplex);
    return CurrentMultiplex;
}

void TuningCurrentMultiplexSet(Multiplex_t *multiplex)
{
    TSFilter_t *tsFilter = MainTSFilterGet();
    PIDFilter_t *primaryServiceFilter;

    TSFilterLock(tsFilter);
    LogModule(LOG_DEBUG, TUNING, "Writing changes back to database.\n");
    CacheWriteback();
    TSFilterUnLock(tsFilter);

    LogModule(LOG_DEBUGV, TUNING, "Disabling filters\n");
    TSFilterEnable(tsFilter, FALSE);

    primaryServiceFilter = TSFilterFindPIDFilter(tsFilter, PrimaryService, ServicePIDFilterType);
    ServiceFilterServiceSet(primaryServiceFilter, NULL);

    TuneMultiplex(multiplex);

    TSFilterZeroStats(tsFilter);

    /*
     * Inform any interested parties that we have now changed the current
     * service.
     */
    ChannelChangedDoCallbacks((Multiplex_t *)CurrentMultiplex, NULL);
    EventsFireEventListeners(serviceChangedEvent, NULL);

    LogModule(LOG_DEBUGV, TUNING, "Enabling filters\n");
    TSFilterEnable(tsFilter, TRUE);
}

/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/

static void ChannelChangedDoCallbacks(Multiplex_t *multiplex, Service_t *service)
{
    ListIterator_t iterator;
    for (ListIterator_Init(iterator, ChannelChangedCallbacksList);
        ListIterator_MoreEntries(iterator);ListIterator_Next(iterator))
    {
        PluginChannelChanged_t callback = ListIterator_Current(iterator);
        callback(multiplex, service);
    }
}

static void TuneMultiplex(Multiplex_t *multiplex)
{
    struct dvb_frontend_parameters feparams;
    DVBDiSEqCSettings_t diseqc;
    DVBAdapter_t *dvbAdapter = MainDVBAdapterGet();
    TSFilter_t *tsFilter = MainTSFilterGet();

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

    LogModule(LOG_DEBUGV,TUNING, "Informing TSFilter multiplex has changed!\n");
    TSFilterMultiplexChanged(tsFilter, CurrentMultiplex);

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
