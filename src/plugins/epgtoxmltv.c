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
* Typedefs                                                                     *
*******************************************************************************/
typedef struct ServiceMultiplexInfo_s
{
    Service_t *service;
    Multiplex_t *mux;
}ServiceMultiplexInfo_t;

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void CommandDump(int argc, char **argv);
static void DumpChannels(List_t *infoList);
static void DumpProgrammes(List_t *infoList);
static void DumpServiceProgrammes(Multiplex_t *multiplex, Service_t *service);
static void DumpProgramme(Multiplex_t *multiplex, Service_t *service, EPGEvent_t *event);
static void PrintXmlified(char *text);
static List_t *GetServiceMultiplexInfo(void);
static void ServiceMultiplexInfoDestructor(void *obj);
/*******************************************************************************
* Plugin Setup                                                                 *
*******************************************************************************/
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
    List_t *infoList;
    ObjectRegisterTypeDestructor(ServiceMultiplexInfo_t, ServiceMultiplexInfoDestructor);
    infoList = GetServiceMultiplexInfo();
    CommandPrintf("<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n");
    CommandPrintf("<tv generator-info-name=\"DVBStreamer-EPGSchedule\">\n");
    
    DumpChannels(infoList);
    DumpProgrammes(infoList);
    CommandPrintf("</tv>\n");
    ObjectListFree(infoList);
}
static void DumpChannels(List_t *infoList)
{
    ListIterator_t iterator;
    ServiceMultiplexInfo_t *info;
    Service_t *service;
    Multiplex_t *multiplex;
    
    for (ListIterator_Init(iterator, infoList);
         ListIterator_MoreEntries(iterator);
         ListIterator_Next(iterator))
    {
        info = ListIterator_Current(iterator);
        service = info->service;
        multiplex = info->mux;
        CommandPrintf("<channel id=\"%04x.%04x.%04x\">\n",
            multiplex->networkId, multiplex->tsId, service->id);
        CommandPrintf("<display-name>");
        PrintXmlified(service->name);
        CommandPrintf("</display-name>\n");
        CommandPrintf("</channel>\n");
    }
}

static void DumpProgrammes(List_t *infoList)
{
    ListIterator_t iterator;
    ServiceMultiplexInfo_t *info;

    EPGDBaseTransactionStart();
    
    for (ListIterator_Init(iterator, infoList);
         ListIterator_MoreEntries(iterator);
         ListIterator_Next(iterator))
    {
        info = ListIterator_Current(iterator);
        DumpServiceProgrammes(info->mux, info->service);
    }
    EPGDBaseTransactionCommit();
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

    CommandPrintf("<programme start=\"%04d%02d%02d%02d%02d%02d +0000\" stop=\"%04d%02d%02d%02d%02d%02d +0000\" channel=\"%04x.%04x.%04x\">\n",
                    event->startTime.tm_year + 1900, event->startTime.tm_mon + 1, event->startTime.tm_mday,
                    event->startTime.tm_hour, event->startTime.tm_min, event->startTime.tm_sec,
                    event->endTime.tm_year + 1900, event->endTime.tm_mon + 1, event->endTime.tm_mday,
                    event->endTime.tm_hour, event->endTime.tm_min, event->endTime.tm_sec,
                    multiplex->networkId, multiplex->tsId, service->id);

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

    /* output seriesid and programid fields */
    enumerator = EPGDBaseDetailGet(&serviceRef, event->eventId, "content");
    do
    {
        detail = EPGDBaseDetailGetNext(enumerator);
        if (detail)
        {
            CommandPrintf("<content lang=\"%s\">", detail->lang);
            PrintXmlified(detail->value);
            CommandPrintf("</content>\n");
            ObjectRefDec(detail);            
        }
    }while(detail && !ExitProgram);
    EPGDBaseEnumeratorDestroy(enumerator);
    enumerator = EPGDBaseDetailGet(&serviceRef, event->eventId, "series");
    do
    {
        detail = EPGDBaseDetailGetNext(enumerator);
        if (detail)
        {
            CommandPrintf("<series lang=\"%s\">", detail->lang);
            PrintXmlified(detail->value);
            CommandPrintf("</series>\n");
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
        if (strlen(temp) + bufferIndex >= sizeof(buffer) - 1)
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

static List_t *GetServiceMultiplexInfo(void)
{
    Multiplex_t *multiplex;
    Service_t *service;
    List_t *multiplexes;
    List_t *services;
    ListIterator_t muxIterator;
    ListIterator_t serviceIterator;    
    ServiceMultiplexInfo_t *info;
    
    List_t *result = ObjectListCreate();
    
    DBaseTransactionBegin();

    multiplexes = MultiplexListAll();
    
    for (ListIterator_Init(muxIterator, multiplexes); 
         ListIterator_MoreEntries(muxIterator);
         ListIterator_Next(muxIterator))
    {
        multiplex = ListIterator_Current(muxIterator);
        services = ServiceListForMultiplex(multiplex);

        for (ListIterator_Init(serviceIterator, services); 
             ListIterator_MoreEntries(serviceIterator);
             ListIterator_Next(serviceIterator))
        {
            service = ListIterator_Current(serviceIterator);
            info = ObjectCreateType(ServiceMultiplexInfo_t);
            if (info)
            {
                info->service = service;
                info->mux = multiplex;
                MultiplexRefInc(multiplex);
                ListAdd(result, info);
            }
        }
        /* Just free the list not the contained objects */
        ListFree(services, NULL);
    }
    /* This won't actually free the objects, but reduces the ref count by 1 
     * so that can be free'd later when the returned list is free'd.
     */
    ObjectListFree(multiplexes);   

    DBaseTransactionCommit();
    return result;
}

static void ServiceMultiplexInfoDestructor(void *obj)
{
    ServiceMultiplexInfo_t *info = obj;
    ServiceRefDec(info->service);
    MultiplexRefDec(info->mux);
}