/*
Copyright (C) 2009  Adam Charrett

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

cmd_epg.c

Command functions to access EPG information.

*/
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include <getopt.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>

#include "commands.h"
#include "logging.h"
#include "epgchannel.h"
#include "main.h"
#include "utf8.h"


/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static void CommandEPGData(int argc, char **argv);
static void PrintXmlified(char *text);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
Command_t CommandEPGInfo[] =
{
    {
        "epgdata",
        0, 0,
        "Register to receive EPG data in XML format.",
        "EPG data is output to the command context in XML format until DVBStreamer"
        "terminates or the command context is closed (ie the socket is disconnected).",
        CommandEPGData
    },
    COMMANDS_SENTINEL
};

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
void CommandInstallEPG(void)
{
    CommandRegisterCommands(CommandEPGInfo);
}

void CommandUnInstallEPG(void)
{
    CommandUnRegisterCommands(CommandEPGInfo); 
}

/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/
static void CommandEPGData(int argc, char **argv)
{
    bool connected = TRUE;
    MessageQ_t msgQ = MessageQCreate();
    char startTimeStr[25];
    char endTimeStr[25];
    
    EPGChannelRegisterListener(msgQ);
    if (CommandPrintf("<epg>\n") < 0)
    {
        connected = FALSE;
    }
    while (connected && !MessageQIsQuitSet(msgQ) && !ExitProgram)
    {
        EPGChannelMessage_t *msg = MessageQReceiveTimed(msgQ, 400);
        if (msg != NULL)
        {
            CommandContext_t *context = CommandContextGet();
            CommandPrintf("<event net=\"0x%04x\" ts=\"0x%04x\" source=\"0x%04x\" event=\"0x%08x\">\n",
                msg->eventRef.serviceRef.netId, msg->eventRef.serviceRef.tsId, msg->eventRef.serviceRef.serviceId,
                msg->eventRef.eventId);
            switch(msg->type)
            {
                case EPGChannelMessageType_Event:
                    strftime(startTimeStr, sizeof(startTimeStr), "%Y-%m-%d %T", &msg->data.event.startTime);
                    strftime(endTimeStr, sizeof(startTimeStr), "%Y-%m-%d %T", &msg->data.event.endTime);                        
                    CommandPrintf("<new start=\"%s\" end=\"%s\" ca=\"%s\"/>\n",
                               startTimeStr, endTimeStr, msg->data.event.ca ? "yes":"no");
                    break;
                case EPGChannelMessageType_Detail:
                    CommandPrintf("<detail lang=\"%s\" name=\"%s\">",
                             msg->data.detail.lang,  msg->data.detail.name);
                    PrintXmlified(msg->data.detail.value);
                    CommandPrintf("</detail>\n");
                    break;
                case EPGChannelMessageType_Rating:
                    CommandPrintf("<rating system=\"%s\" value=\"%s\"/>\n", 
                                msg->data.rating.system, msg->data.rating.rating);
                    break;
            }


            CommandPrintf("</event>\n");
            if (fflush(context->outfp) != 0)
            {
                connected = FALSE;
            }
            LogModule(LOG_INFO, "EPG Data", "connected = %d", connected);
            ObjectRefDec(msg);
        }
    }
    
    EPGChannelUnregisterListener(msgQ);
    MessageQDestroy(msgQ);
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
