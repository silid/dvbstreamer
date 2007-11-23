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
#include "utf8.h"
#include "plugin.h"
#include "epgdbase.h"
#include "dbase.h"

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
static void PrintXmlified(char *text);
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
    CommandPrintf("<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n");    
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
            CommandPrintf("<channel id=\"");
            PrintXmlified(service->name);
            CommandPrintf("\">\n");
            CommandPrintf("<display-name>");
            PrintXmlified(service->name);
            CommandPrintf("</display-name>\n", service->name);
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
    List_t *multiplexes = ListCreate();
    ListIterator_t iterator;
    Multiplex_t *multiplex;
    DBaseTransactionBegin();
    EPGDBaseTransactionStart();
    do
    {
        multiplex = MultiplexGetNext(enumerator);
        if (multiplex)
        {
            ListAdd(multiplexes, multiplex);
        }
    }while(multiplex && !ExitProgram);
    MultiplexEnumeratorDestroy(enumerator);

    for (ListIterator_Init(iterator, multiplexes); ListIterator_MoreEntries(iterator);ListIterator_Next(iterator))
    {
        multiplex = ListIterator_Current(iterator);
        DumpMultiplexProgrammes(multiplex);
        MultiplexRefDec(multiplex);
    }
    ListFree(multiplexes,NULL);
    EPGDBaseTransactionCommit();
    DBaseTransactionCommit();
}

static void DumpMultiplexProgrammes(Multiplex_t *multiplex)
{
    Service_t *service;
    ServiceEnumerator_t enumerator = ServiceEnumeratorForMultiplex(multiplex);
    List_t *services = ListCreate();
    ListIterator_t iterator;
    do
    {
        service = ServiceGetNext(enumerator);
        if (service)
        {
            ListAdd(services, service);
        }
    }
    while(service && !ExitProgram);
    ServiceEnumeratorDestroy(enumerator); 
    
    for (ListIterator_Init(iterator, services); ListIterator_MoreEntries(iterator);ListIterator_Next(iterator))
    {
        service = ListIterator_Current(iterator);
        DumpServiceProgrammes(multiplex, service);
        ServiceRefDec(service);
    }    
    ListFree(services, NULL);

}

static void DumpServiceProgrammes(Multiplex_t *multiplex, Service_t *service)
{
    EPGEvent_t *event;
    EPGDBaseEnumerator_t enumerator;
    EPGServiceRef_t serviceRef;
    serviceRef.netId = multiplex->networkId;
    serviceRef.tsId = multiplex->tsId;
    serviceRef.serviceId = service->source;

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
    serviceRef.serviceId = service->source;

    CommandPrintf("<programme start=\"%04d%02d%02d%02d%02d%02d +0000\" stop=\"%04d%02d%02d%02d%02d%02d +0000\" channel=\"%s\">\n",
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
            CommandPrintf("<title lang=\"%s\">", detail->lang);
            PrintXmlified(detail->value);
            CommandPrintf("</title>\n");
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
            CommandPrintf("<desc lang=\"%s\">", detail->lang);
            PrintXmlified(detail->value);
            CommandPrintf("</desc>\n");
            ObjectRefDec(detail);            
        }
    }while(detail && !ExitProgram);
    EPGDBaseEnumeratorDestroy(enumerator);

    CommandPrintf("</programme>\n");
}

static void PrintXmlified(char *text)
{
    char buffer[256];
    char temp[10];
    int bufferIndex = 0;
    int i;
    int utf8CharLen;
    int len = strlen(text);

    buffer[0] = 0;
    for (i = 0; i < len;)
    {
        unsigned int ch = UTF8_nextchar(text, &i);
        switch (ch) {
            case '\t':
            case '\n':
            case ' ' ... '%': // &
            case '\'' ... ';': // <
            case '=': // >
            case '?' ... 0x7E:
                temp[0] = (char)ch;
                temp[1] = 0;
                break;
            case '&':
                strcpy(temp, "&amp;");
                break;
            case '<':
                strcpy(temp, "&lt;");
                break;
            case '>':
                strcpy(temp, "&gt;");
                break;
            case 0x0000 ... 0x0008:
            case 0x000B ... 0x001F:
            case 0x007F:
                fprintf(stderr, "Illegal char %04x\n", i);
            default:
                utf8CharLen = UTF8_wc_toutf8(temp, ch);
                temp[utf8CharLen] = 0;
                break;
        } // switch
        if (strlen(temp) + bufferIndex > sizeof(buffer))
        {
            CommandPrintf("%s", buffer);
            bufferIndex = 0;
            buffer[0] = 0;
        }
        strcat(buffer, temp);
        bufferIndex += strlen(temp);
    }
    if (bufferIndex)
    {
        CommandPrintf("%s", buffer);
    }
}
