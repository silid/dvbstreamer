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
#include "main.h"
#include "tuning.h"
#include "cache.h"
#include "logging.h"
#include "dvb.h"
#include "ts.h"
#include "outputs.h"

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void ChannelChangedDoCallbacks(Multiplex_t *multiplex, Service_t *service);
static void TuneMultiplex(Multiplex_t *multiplex);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static Multiplex_t *CurrentMultiplex = NULL;
static Service_t *CurrentService = NULL;
static List_t *ChannelChangedCallbacksList = NULL;

static const char TUNING[] = "tuning";

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/

int TuningInit(void)
{
    ChannelChangedCallbacksList = ListCreate();
    return 0;
}

int TuningDeinit(void)
{
    ListFree(ChannelChangedCallbacksList,NULL);
    MultiplexRefDec(CurrentMultiplex);
    ServiceRefDec(CurrentService);
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

/*
 * Find the service named <name> and tune to the new frequency for the multiplex the service is
 * on (if required) and then select the new service id to filter packets for.
 */
Service_t *TuningCurrentServiceSet(char *name)
{
    Output_t *primaryServiceOutput;
    Multiplex_t *multiplex;
    Service_t *service;
    TSFilter_t *tsFilter = MainTSFilterGet();

    TSFilterLock(tsFilter);
    LogModule(LOG_DEBUG, TUNING, "Writing changes back to database.\n");
    CacheWriteback();
    TSFilterUnLock(tsFilter);

    service = CacheServiceFindName(name, &multiplex);
    if (!service)
    {
        return NULL;
    }

    LogModule(LOG_DEBUG, TUNING, "Service found id:0x%04x Multiplex:%d\n", service->id, service->multiplexUID);
    if ((CurrentService == NULL) || (!ServiceAreEqual(service,CurrentService)))
    {
        printlog(LOG_DEBUGV,"Disabling filters\n");
        TSFilterEnable(tsFilter, FALSE);

        if (CurrentMultiplex)
        {
            LogModule(LOG_DEBUG, TUNING, "Current Multiplex frequency = %d TS id = %d\n",CurrentMultiplex->freq, CurrentMultiplex->tsId);
        }
        else
        {
            LogModule(LOG_DEBUG, TUNING, "No current Multiplex!\n");
        }

        if (multiplex)
        {
            LogModule(LOG_DEBUG, TUNING, "New Multiplex frequency =%d TS id = %d\n",multiplex->freq, multiplex->tsId);
        }
        else
        {
            LogModule(LOG_DEBUG, TUNING, "No new Multiplex!\n");
        }

        if (CurrentService)
        {
            ServiceRefDec(CurrentService);
        }

        if ((CurrentMultiplex!= NULL) && MultiplexAreEqual(multiplex, CurrentMultiplex))
        {
            LogModule(LOG_DEBUGV, TUNING, "Same multiplex\n");
            CurrentService = service;
        }
        else
        {
            TuneMultiplex(multiplex);
            
            CurrentService = CacheServiceFindId(service->id);
            ServiceRefDec(service);
        }

        TSFilterZeroStats(tsFilter);

        primaryServiceOutput = OutputFind(PrimaryService, OutputType_Service);
        OutputSetService(primaryServiceOutput, (Service_t*)CurrentService);

        /*
         * Inform any interested parties that we have now changed the current
         * service.
         */
        ChannelChangedDoCallbacks((Multiplex_t *)CurrentMultiplex, (Service_t *)CurrentService);

        LogModule(LOG_DEBUGV, TUNING, "Enabling filters\n");
        TSFilterEnable(tsFilter, TRUE);
    }
    else
    {
        ServiceRefDec(service);
    }
    MultiplexRefDec(multiplex);

    return TuningCurrentServiceGet();
}

Multiplex_t *TuningCurrentMultiplexGet(void)
{
    MultiplexRefInc(CurrentMultiplex);
    return CurrentMultiplex;
}

void TuningCurrentMultiplexSet(Multiplex_t *multiplex)
{
    Output_t *primaryServiceOutput;
    TSFilter_t *tsFilter = MainTSFilterGet();
    
    TSFilterLock(tsFilter);
    LogModule(LOG_DEBUG, TUNING, "Writing changes back to database.\n");
    CacheWriteback();
    TSFilterUnLock(tsFilter);

    LogModule(LOG_DEBUGV, TUNING, "Disabling filters\n");
    TSFilterEnable(tsFilter, FALSE);
    
    primaryServiceOutput = OutputFind(PrimaryService, OutputType_Service);
    OutputSetService(primaryServiceOutput, NULL);

    TuneMultiplex(multiplex);

    TSFilterZeroStats(tsFilter);

    /*
     * Inform any interested parties that we have now changed the current
     * service.
     */
    ChannelChangedDoCallbacks((Multiplex_t *)CurrentMultiplex, NULL);

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
    DVBFrontEndTune(dvbAdapter, &feparams, &diseqc);

    LogModule(LOG_DEBUGV,TUNING, "Informing TSFilter multiplex has changed!\n");
    TSFilterMultiplexChanged(tsFilter, CurrentMultiplex);
}

