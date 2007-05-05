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

epgtoxmltv.c

Plugin to dump the EPG Database out in XMLTV format.

*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "main.h"

#include "plugin.h"
#include "epgdbase.h"

#include "list.h"
#include "logging.h"

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void CommandDump(int argc, char **argv);
static void DumpChannels(void);
static void DumpProgrammes(void);
static void DumpMultiplexProgrammes(Multiplex_t *multiplex);
static void DumpServiceProgrammes(Multiplex_t *multiplex, Service_t *service);
static void DumpProgramme(Multiplex_t *multiplex, Service_t *service, EPGEvent_t *event);

/*******************************************************************************
* Plugin Setup                                                                 *
*******************************************************************************/
#ifdef __CYGWIN__
#define PluginInterface EPGtoXMLTVPluginInterface
#endif
PLUGIN_COMMANDS(
    {
        "dumpxmltv",
        FALSE, 0, 0,
        "Dump the EPG Database in XMLTV format.",
        "Output the contents of the EPG Database in XMLTV format.",
        CommandDump
    }
);

PLUGIN_INTERFACE_C(
    PLUGIN_FOR_ALL,
    "EPGtoXMLTV", "0.1", 
    "Plugin to dump the EPG Database out in XMLTV format.", 
    "charrea6@users.sourceforge.net"
    );

/*******************************************************************************
* Command Functions                                                            *
*******************************************************************************/
static void CommandDump(int argc, char **argv)
{
    CommandPrintf("<tv generator-info-name=\"DVBStreamer-EPGSchedule\">\n");
    DumpChannels();
    DumpProgrammes();
    CommandPrintf("</tv>\n");
}
static void DumpChannels(void)
{
    ServiceEnumerator_t enumerator = ServiceEnumeratorGet();
    Service_t *service;
    do
    {
        service = ServiceGetNext(enumerator);
        if (service)
        {
            CommandPrintf("<channel id=\"%s\">\n", service->name);
            CommandPrintf("<display-name>%s</display-name>\n", service->name);
            CommandPrintf("</channel>\n");
            ServiceRefDec(service);
        }
    }
    while(service);
    ServiceEnumeratorDestroy(enumerator);
}

static void DumpProgrammes(void)
{
    MultiplexEnumerator_t enumerator = MultiplexEnumeratorGet();
    Multiplex_t *multiplex;
    EPGDBaseTransactionStart();
    do
    {
        multiplex = MultiplexGetNext(enumerator);
        if (multiplex)
        {
            DumpMultiplexProgrammes(multiplex);
            MultiplexRefDec(multiplex);
        }
    }while(multiplex && !ExitProgram);
    MultiplexEnumeratorDestroy(enumerator);
    EPGDBaseTransactionCommit();
}

static void DumpMultiplexProgrammes(Multiplex_t *multiplex)
{
    Service_t *service;
    ServiceEnumerator_t enumerator = ServiceEnumeratorForMultiplex(multiplex);

    do
    {
        service = ServiceGetNext(enumerator);
        if (service)
        {
            DumpServiceProgrammes(multiplex, service);
            ServiceRefDec(service);
        }
    }
    while(service && !ExitProgram);
    ServiceEnumeratorDestroy(enumerator); 
}

static void DumpServiceProgrammes(Multiplex_t *multiplex, Service_t *service)
{
    EPGEvent_t *event;
    EPGDBaseEnumerator_t enumerator;
    EPGServiceRef_t serviceRef;
    serviceRef.netId = multiplex->networkId;
    serviceRef.tsId = multiplex->tsId;
    serviceRef.serviceId = service->id;

    enumerator = EPGDBaseEventEnumeratorGetService(&serviceRef);
    do
    {
        event = EPGDBaseEventGetNext(enumerator);
        if (event)
        {
            DumpProgramme(multiplex, service, event);
            ObjectRefDec(event);
        }
    }while(event && !ExitProgram);

    EPGDBaseEnumeratorDestroy(enumerator);
}

static void DumpProgramme(Multiplex_t *multiplex, Service_t *service, EPGEvent_t *event)
{
    EPGDBaseEnumerator_t enumerator;
    EPGEventDetail_t *detail;
    EPGServiceRef_t serviceRef;
    serviceRef.netId = multiplex->networkId;
    serviceRef.tsId = multiplex->tsId;
    serviceRef.serviceId = service->id;
    
    CommandPrintf("<programme start=\"%04d%02d%02d%02d%02d%02d\" stop=\"%04d%02d%02d%02d%02d%02d\" channel=\"%s\">\n",
                    event->startTime.tm_year + 1900, event->startTime.tm_mon + 1, event->startTime.tm_mday,
                    event->startTime.tm_hour, event->startTime.tm_min, event->startTime.tm_sec,
                    event->endTime.tm_year + 1900, event->endTime.tm_mon + 1, event->endTime.tm_mday,
                    event->endTime.tm_hour, event->endTime.tm_min, event->endTime.tm_sec,                    
                    service->name);
    enumerator = EPGDBaseDetailGet(&serviceRef, event->eventId, EPG_EVENT_DETAIL_TITLE);
    do
    {
        detail = EPGDBaseDetailGetNext(enumerator);
        if (detail)
        {
            CommandPrintf("<title lang=\"%s\">%s</title>\n", detail->lang, detail->value);
            ObjectRefDec(detail);
        }
    }while(detail && !ExitProgram);
    
    EPGDBaseEnumeratorDestroy(enumerator);
    enumerator = EPGDBaseDetailGet(&serviceRef, event->eventId, EPG_EVENT_DETAIL_DESCRIPTION);
    do
    {
        detail = EPGDBaseDetailGetNext(enumerator);
        if (detail)
        {
            CommandPrintf("<desc lang=\"%s\">%s</desc>\n", detail->lang, detail->value);
            ObjectRefDec(detail);            
        }
    }while(detail && !ExitProgram);
    EPGDBaseEnumeratorDestroy(enumerator);
    
    CommandPrintf("</programme>\n");
}

