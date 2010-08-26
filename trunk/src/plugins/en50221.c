/*****************************************************************************
 * en50221.c : implementation of the transport, session and applications
 * layers of EN 50 221
 *****************************************************************************
 * Copyright (C) 2004-2005 VideoLAN
 *               2010 A Charrett
 * $Id: en50221.c 105 2010-02-20 10:53:29Z md $
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
 * Based on code from libdvbci Copyright (C) 2000 Klaus Schmidinger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.    See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA    02111, USA.
 *****************************************************************************/

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <errno.h>
#include <time.h>

/* DVB Card Drivers */
#include <linux/dvb/ca.h>
#include "dvbpsi/dvbpsi.h"
#include "dvbpsi/descriptor.h"
#include "dvbpsi/pmt.h"

#include "logging.h"

#include "en50221.h"

/*****************************************************************************
 * DVBStreamer Porting
 *****************************************************************************/

static char EN50221[]= "EN50221";

#define msg_Dbg(_u, msg...)\
    LogModule(LOG_DEBUG, EN50221, msg)


#define msg_Info(_u, msg...)\
    LogModule(LOG_INFO, EN50221, msg)

#define msg_Warn(_u, msg...)\
    LogModule(LOG_INFO, EN50221, msg)

#define msg_Err(_u, msg...)\
    LogModule(LOG_ERROR, EN50221, msg)

typedef int64_t mtime_t;
int b_slow_cam = 0;
mtime_t i_ca_timeout = 0;

/*****************************************************************************
 * mdate
 *****************************************************************************/
mtime_t mdate( void )
{
    struct timespec ts;

    /* Try to use POSIX monotonic clock if available */
    if( clock_gettime( CLOCK_MONOTONIC, &ts ) == EINVAL )
        /* Run-time fallback to real-time clock (always available) */
        (void)clock_gettime( CLOCK_REALTIME, &ts );

    return ((mtime_t)ts.tv_sec * (mtime_t)1000000)
            + (mtime_t)(ts.tv_nsec / 1000);
}

/*****************************************************************************
 * msleep
 *****************************************************************************/
void msleep( mtime_t delay )
{
    struct timespec ts;
    ts.tv_sec = delay / 1000000;
    ts.tv_nsec = (delay % 1000000) * 1000;

    int val;
    while ( ( val = clock_nanosleep( CLOCK_MONOTONIC, 0, &ts, &ts ) ) == EINTR );
    if( val == EINVAL )
    {
        ts.tv_sec = delay / 1000000;
        ts.tv_nsec = (delay % 1000000) * 1000;
        while ( clock_nanosleep( CLOCK_REALTIME, 0, &ts, &ts ) == EINTR );
    }
}

/*****************************************************************************
 * Macros
 *****************************************************************************/

#define TAB_APPEND( count, tab, p )             \
    if( (count) > 0 )                           \
    {                                           \
        (tab) = realloc( tab, sizeof( void ** ) * ( (count) + 1 ) ); \
    }                                           \
    else                                        \
    {                                           \
        (tab) = malloc( sizeof( void ** ) );    \
    }                                           \
    (tab)[count] = (p);        \
    (count)++

/*****************************************************************************
 * Local declarations
 *****************************************************************************/
#undef DEBUG_TPDU
#define HLCI_WAIT_CAM_READY 0
#define CAM_PROG_MAX MAX_PROGRAMS
#define CAPMT_WAIT 100 /* ms */

typedef struct en50221_session_t
{
    int i_slot;
    int i_resource_id;
    void (* pf_handle)( access_t *, int, uint8_t *, int );
    void (* pf_close)( access_t *, int );
    void (* pf_manage)( access_t *, int );
    void *p_sys;
} en50221_session_t;

int i_ca_handle = 0;
int i_ca_type = -1;
static int i_nb_slots = 0;
static bool pb_active_slot[MAX_CI_SLOTS];
static bool pb_tc_has_data[MAX_CI_SLOTS];
static bool pb_slot_mmi_expected[MAX_CI_SLOTS];
static bool pb_slot_mmi_undisplayed[MAX_CI_SLOTS];
static en50221_session_t p_sessions[MAX_SESSIONS];

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
static void ResourceManagerOpen( access_t * p_access, int i_session_id );
static void ApplicationInformationOpen( access_t * p_access, int i_session_id );
static void ConditionalAccessOpen( access_t * p_access, int i_session_id );
static void DateTimeOpen( access_t * p_access, int i_session_id );
static void MMIOpen( access_t * p_access, int i_session_id );

/*****************************************************************************
 * Utility functions
 *****************************************************************************/
#define SIZE_INDICATOR 0x80

static uint8_t *GetLength( uint8_t *p_data, int *pi_length )
{
    *pi_length = *p_data++;

    if ( (*pi_length & SIZE_INDICATOR) != 0 )
    {
        int l = *pi_length & ~SIZE_INDICATOR;
        int i;

        *pi_length = 0;
        for ( i = 0; i < l; i++ )
            *pi_length = (*pi_length << 8) | *p_data++;
    }

    return p_data;
}

static uint8_t *SetLength( uint8_t *p_data, int i_length )
{
    uint8_t *p = p_data;

    if ( i_length < 128 )
    {
        *p++ = i_length;
    }
    else if ( i_length < 256 )
    {
        *p++ = SIZE_INDICATOR | 0x1;
        *p++ = i_length;
    }
    else if ( i_length < 65536 )
    {
        *p++ = SIZE_INDICATOR | 0x2;
        *p++ = i_length >> 8;
        *p++ = i_length & 0xff;
    }
    else if ( i_length < 16777216 )
    {
        *p++ = SIZE_INDICATOR | 0x3;
        *p++ = i_length >> 16;
        *p++ = (i_length >> 8) & 0xff;
        *p++ = i_length & 0xff;
    }
    else
    {
        *p++ = SIZE_INDICATOR | 0x4;
        *p++ = i_length >> 24;
        *p++ = (i_length >> 16) & 0xff;
        *p++ = (i_length >> 8) & 0xff;
        *p++ = i_length & 0xff;
    }

    return p;
}


/*
 * Transport layer
 */

#define MAX_TPDU_SIZE  4096
#define MAX_TPDU_DATA  (MAX_TPDU_SIZE - 4)

#define DATA_INDICATOR 0x80

#define T_SB           0x80
#define T_RCV          0x81
#define T_CREATE_TC    0x82
#define T_CTC_REPLY    0x83
#define T_DELETE_TC    0x84
#define T_DTC_REPLY    0x85
#define T_REQUEST_TC   0x86
#define T_NEW_TC       0x87
#define T_TC_ERROR     0x88
#define T_DATA_LAST    0xA0
#define T_DATA_MORE    0xA1

static void Dump( bool b_outgoing, uint8_t *p_data, int i_size )
{
#ifdef DEBUG_TPDU
    int i;
#define MAX_DUMP 256
    fprintf(stderr, "%s ", b_outgoing ? "-->" : "<--");
    for ( i = 0; i < i_size && i < MAX_DUMP; i++)
        fprintf(stderr, "%02X ", p_data[i]);
    fprintf(stderr, "%s\n", i_size >= MAX_DUMP ? "..." : "");
#endif
}

/*****************************************************************************
 * TPDUSend
 *****************************************************************************/
static int TPDUSend( access_t * p_access, uint8_t i_slot, uint8_t i_tag,
                     const uint8_t *p_content, int i_length )
{
    uint8_t i_tcid = i_slot + 1;
    uint8_t p_data[MAX_TPDU_SIZE];
    int i_size;

    i_size = 0;
    p_data[0] = i_slot;
    p_data[1] = i_tcid;
    p_data[2] = i_tag;

    switch ( i_tag )
    {
    case T_RCV:
    case T_CREATE_TC:
    case T_CTC_REPLY:
    case T_DELETE_TC:
    case T_DTC_REPLY:
    case T_REQUEST_TC:
        p_data[3] = 1; /* length */
        p_data[4] = i_tcid;
        i_size = 5;
        break;

    case T_NEW_TC:
    case T_TC_ERROR:
        p_data[3] = 2; /* length */
        p_data[4] = i_tcid;
        p_data[5] = p_content[0];
        i_size = 6;
        break;

    case T_DATA_LAST:
    case T_DATA_MORE:
    {
        /* i_length <= MAX_TPDU_DATA */
        uint8_t *p = p_data + 3;
        p = SetLength( p, i_length + 1 );
        *p++ = i_tcid;

        if ( i_length )
            memcpy( p, p_content, i_length );
            i_size = i_length + (p - p_data);
        }
        break;

    default:
        break;
    }
    Dump( true, p_data, i_size );

    if ( write( i_ca_handle, p_data, i_size ) != i_size )
    {
        msg_Err( p_access, "cannot write to CAM device (%m)" );
        return -1;
    }

    return 0;
}


/*****************************************************************************
 * TPDURecv
 *****************************************************************************/
#define CAM_READ_TIMEOUT  3500 // ms

static int TPDURecv( access_t * p_access, uint8_t i_slot, uint8_t *pi_tag,
                     uint8_t *p_data, int *pi_size )
{
    uint8_t i_tcid = i_slot + 1;
    int i_size;
    struct pollfd pfd[1];

    pfd[0].fd = i_ca_handle;
    pfd[0].events = POLLIN;
    if ( !(poll(pfd, 1, CAM_READ_TIMEOUT) > 0 && (pfd[0].revents & POLLIN)) )
    {
        msg_Err( p_access, "cannot poll from CAM device" );
        return -1;
    }

    if ( pi_size == NULL )
    {
        p_data = malloc( MAX_TPDU_SIZE );
    }

    for ( ; ; )
    {
        i_size = read( i_ca_handle, p_data, MAX_TPDU_SIZE );

        if ( i_size >= 0 || errno != EINTR )
            break;
    }

    if ( i_size < 5 )
    {
        msg_Err( p_access, "cannot read from CAM device (%d:%m)", i_size );
        if( pi_size == NULL )
            free( p_data );
        return -1;
    }

    if ( p_data[1] != i_tcid )
    {
        msg_Err( p_access, "invalid read from CAM device (%d instead of %d)",
                 p_data[1], i_tcid );
        if( pi_size == NULL )
            free( p_data );
        return -1;
    }

    *pi_tag = p_data[2];
    pb_tc_has_data[i_slot] = (i_size >= 4
                                      && p_data[i_size - 4] == T_SB
                                      && p_data[i_size - 3] == 2
                                      && (p_data[i_size - 1] & DATA_INDICATOR))
                                        ?  true : false;

    Dump( false, p_data, i_size );

    if ( pi_size == NULL )
        free( p_data );
    else
        *pi_size = i_size;

    return 0;
}


/*
 * Session layer
 */

#define ST_SESSION_NUMBER           0x90
#define ST_OPEN_SESSION_REQUEST     0x91
#define ST_OPEN_SESSION_RESPONSE    0x92
#define ST_CREATE_SESSION           0x93
#define ST_CREATE_SESSION_RESPONSE  0x94
#define ST_CLOSE_SESSION_REQUEST    0x95
#define ST_CLOSE_SESSION_RESPONSE   0x96

#define SS_OK             0x00
#define SS_NOT_ALLOCATED  0xF0

#define RI_RESOURCE_MANAGER            0x00010041
#define RI_APPLICATION_INFORMATION     0x00020041
#define RI_CONDITIONAL_ACCESS_SUPPORT  0x00030041
#define RI_HOST_CONTROL                0x00200041
#define RI_DATE_TIME                   0x00240041
#define RI_MMI                         0x00400041

static int ResourceIdToInt( uint8_t *p_data )
{
    return ((int)p_data[0] << 24) | ((int)p_data[1] << 16)
            | ((int)p_data[2] << 8) | p_data[3];
}

/*****************************************************************************
 * SPDUSend
 *****************************************************************************/
static int SPDUSend( access_t * p_access, int i_session_id,
                     uint8_t *p_data, int i_size )
{
    uint8_t *p_spdu = malloc( i_size + 4 );
    uint8_t *p = p_spdu;
    uint8_t i_tag;
    uint8_t i_slot = p_sessions[i_session_id - 1].i_slot;

    *p++ = ST_SESSION_NUMBER;
    *p++ = 0x02;
    *p++ = (i_session_id >> 8);
    *p++ = i_session_id & 0xff;

    memcpy( p, p_data, i_size );

    i_size += 4;
    p = p_spdu;

    while ( i_size > 0 )
    {
        if ( i_size > MAX_TPDU_DATA )
        {
            if ( TPDUSend( p_access, i_slot, T_DATA_MORE, p,
                           MAX_TPDU_DATA ) != 0 )
            {
                msg_Err( p_access, "couldn't send TPDU on session %d",
                         i_session_id );
                free( p_spdu );
                return -1;
            }
            p += MAX_TPDU_DATA;
            i_size -= MAX_TPDU_DATA;
        }
        else
        {
            if ( TPDUSend( p_access, i_slot, T_DATA_LAST, p, i_size )
                    != 0 )
            {
                msg_Err( p_access, "couldn't send TPDU on session %d",
                         i_session_id );
                free( p_spdu );
                return -1;
            }
            i_size = 0;
        }

        if ( TPDURecv( p_access, i_slot, &i_tag, NULL, NULL ) != 0
               || i_tag != T_SB )
        {
            msg_Err( p_access, "couldn't recv TPDU on session %d",
                     i_session_id );
            free( p_spdu );
            return -1;
        }
    }

    free( p_spdu );
    return 0;
}

/*****************************************************************************
 * SessionOpen
 *****************************************************************************/
static void SessionOpen( access_t * p_access, uint8_t i_slot,
                         uint8_t *p_spdu, int i_size )
{
    int i_session_id;
    int i_resource_id = ResourceIdToInt( &p_spdu[2] );
    uint8_t p_response[16];
    int i_status = SS_NOT_ALLOCATED;
    uint8_t i_tag;

    for ( i_session_id = 1; i_session_id <= MAX_SESSIONS; i_session_id++ )
    {
        if ( !p_sessions[i_session_id - 1].i_resource_id )
            break;
    }
    if ( i_session_id == MAX_SESSIONS )
    {
        msg_Err( p_access, "too many sessions !" );
        return;
    }
    p_sessions[i_session_id - 1].i_slot = i_slot;
    p_sessions[i_session_id - 1].i_resource_id = i_resource_id;
    p_sessions[i_session_id - 1].pf_close = NULL;
    p_sessions[i_session_id - 1].pf_manage = NULL;

    if ( i_resource_id == RI_RESOURCE_MANAGER
          || i_resource_id == RI_APPLICATION_INFORMATION
          || i_resource_id == RI_CONDITIONAL_ACCESS_SUPPORT
          || i_resource_id == RI_DATE_TIME
          || i_resource_id == RI_MMI )
    {
        i_status = SS_OK;
    }

    p_response[0] = ST_OPEN_SESSION_RESPONSE;
    p_response[1] = 0x7;
    p_response[2] = i_status;
    p_response[3] = p_spdu[2];
    p_response[4] = p_spdu[3];
    p_response[5] = p_spdu[4];
    p_response[6] = p_spdu[5];
    p_response[7] = i_session_id >> 8;
    p_response[8] = i_session_id & 0xff;

    if ( TPDUSend( p_access, i_slot, T_DATA_LAST, p_response, 9 ) !=
            0 )
    {
        msg_Err( p_access,
                 "SessionOpen: couldn't send TPDU on slot %d", i_slot );
        return;
    }
    if ( TPDURecv( p_access, i_slot, &i_tag, NULL, NULL ) != 0 )
    {
        msg_Err( p_access,
                 "SessionOpen: couldn't recv TPDU on slot %d", i_slot );
        return;
    }

    switch ( i_resource_id )
    {
    case RI_RESOURCE_MANAGER:
        ResourceManagerOpen( p_access, i_session_id ); break;
    case RI_APPLICATION_INFORMATION:
        ApplicationInformationOpen( p_access, i_session_id ); break;
    case RI_CONDITIONAL_ACCESS_SUPPORT:
        ConditionalAccessOpen( p_access, i_session_id ); break;
    case RI_DATE_TIME:
        DateTimeOpen( p_access, i_session_id ); break;
    case RI_MMI:
        MMIOpen( p_access, i_session_id ); break;

    case RI_HOST_CONTROL:
    default:
        msg_Err( p_access, "unknown resource id (0x%x)", i_resource_id );
        p_sessions[i_session_id - 1].i_resource_id = 0;
    }
}

#if 0
/* unused code for the moment - commented out to keep gcc happy */
/*****************************************************************************
 * SessionCreate
 *****************************************************************************/
static void SessionCreate( access_t * p_access, int i_slot, int i_resource_id )
{
    uint8_t p_response[16];
    uint8_t i_tag;
    int i_session_id;

    for ( i_session_id = 1; i_session_id <= MAX_SESSIONS; i_session_id++ )
    {
        if ( !p_sessions[i_session_id - 1].i_resource_id )
            break;
    }
    if ( i_session_id == MAX_SESSIONS )
    {
        msg_Err( p_access, "too many sessions !" );
        return;
    }
    p_sessions[i_session_id - 1].i_slot = i_slot;
    p_sessions[i_session_id - 1].i_resource_id = i_resource_id;
    p_sessions[i_session_id - 1].pf_close = NULL;
    p_sessions[i_session_id - 1].pf_manage = NULL;
    p_sessions[i_session_id - 1].p_sys = NULL;

    p_response[0] = ST_CREATE_SESSION;
    p_response[1] = 0x6;
    p_response[2] = i_resource_id >> 24;
    p_response[3] = (i_resource_id >> 16) & 0xff;
    p_response[4] = (i_resource_id >> 8) & 0xff;
    p_response[5] = i_resource_id & 0xff;
    p_response[6] = i_session_id >> 8;
    p_response[7] = i_session_id & 0xff;

    if ( TPDUSend( p_access, i_slot, T_DATA_LAST, p_response, 4 ) !=
            0 )
    {
        msg_Err( p_access,
                 "SessionCreate: couldn't send TPDU on slot %d", i_slot );
        return;
    }
    if ( TPDURecv( p_access, i_slot, &i_tag, NULL, NULL ) != 0 )
    {
        msg_Err( p_access,
                 "SessionCreate: couldn't recv TPDU on slot %d", i_slot );
        return;
    }
}
#endif

/*****************************************************************************
 * SessionCreateResponse
 *****************************************************************************/
static void SessionCreateResponse( access_t * p_access, uint8_t i_slot,
                                   uint8_t *p_spdu, int i_size )
{
    int i_status = p_spdu[2];
    int i_resource_id = ResourceIdToInt( &p_spdu[3] );
    int i_session_id = ((int)p_spdu[7] << 8) | p_spdu[8];

    if ( i_status != SS_OK )
    {
        msg_Err( p_access, "SessionCreateResponse: failed to open session %d"
                 " resource=0x%x status=0x%x", i_session_id, i_resource_id,
                 i_status );
        p_sessions[i_session_id - 1].i_resource_id = 0;
        return;
    }

    switch ( i_resource_id )
    {
    case RI_RESOURCE_MANAGER:
        ResourceManagerOpen( p_access, i_session_id ); break;
    case RI_APPLICATION_INFORMATION:
        ApplicationInformationOpen( p_access, i_session_id ); break;
    case RI_CONDITIONAL_ACCESS_SUPPORT:
        ConditionalAccessOpen( p_access, i_session_id ); break;
    case RI_DATE_TIME:
        DateTimeOpen( p_access, i_session_id ); break;
    case RI_MMI:
        MMIOpen( p_access, i_session_id ); break;

    case RI_HOST_CONTROL:
    default:
        msg_Err( p_access, "unknown resource id (0x%x)", i_resource_id );
        p_sessions[i_session_id - 1].i_resource_id = 0;
    }
}

/*****************************************************************************
 * SessionSendClose
 *****************************************************************************/
static void SessionSendClose( access_t * p_access, int i_session_id )
{
    uint8_t p_response[16];
    uint8_t i_tag;
    uint8_t i_slot = p_sessions[i_session_id - 1].i_slot;

    p_response[0] = ST_CLOSE_SESSION_REQUEST;
    p_response[1] = 0x2;
    p_response[2] = i_session_id >> 8;
    p_response[3] = i_session_id & 0xff;

    if ( TPDUSend( p_access, i_slot, T_DATA_LAST, p_response, 4 ) !=
            0 )
    {
        msg_Err( p_access,
                 "SessionSendClose: couldn't send TPDU on slot %d", i_slot );
        return;
    }
    if ( TPDURecv( p_access, i_slot, &i_tag, NULL, NULL ) != 0 )
    {
        msg_Err( p_access,
                 "SessionSendClose: couldn't recv TPDU on slot %d", i_slot );
        return;
    }
}

/*****************************************************************************
 * SessionClose
 *****************************************************************************/
static void SessionClose( access_t * p_access, int i_session_id )
{
    uint8_t p_response[16];
    uint8_t i_tag;
    uint8_t i_slot = p_sessions[i_session_id - 1].i_slot;

    if ( p_sessions[i_session_id - 1].pf_close != NULL )
        p_sessions[i_session_id - 1].pf_close( p_access, i_session_id );
    p_sessions[i_session_id - 1].i_resource_id = 0;

    p_response[0] = ST_CLOSE_SESSION_RESPONSE;
    p_response[1] = 0x3;
    p_response[2] = SS_OK;
    p_response[3] = i_session_id >> 8;
    p_response[4] = i_session_id & 0xff;

    if ( TPDUSend( p_access, i_slot, T_DATA_LAST, p_response, 5 ) !=
            0 )
    {
        msg_Err( p_access,
                 "SessionClose: couldn't send TPDU on slot %d", i_slot );
        return;
    }
    if ( TPDURecv( p_access, i_slot, &i_tag, NULL, NULL ) != 0 )
    {
        msg_Err( p_access,
                 "SessionClose: couldn't recv TPDU on slot %d", i_slot );
        return;
    }
}

/*****************************************************************************
 * SPDUHandle
 *****************************************************************************/
static void SPDUHandle( access_t * p_access, uint8_t i_slot,
                        uint8_t *p_spdu, int i_size )
{
    int i_session_id;

    switch ( p_spdu[0] )
    {
    case ST_SESSION_NUMBER:
        if ( i_size <= 4 )
            return;
        i_session_id = ((int)p_spdu[2] << 8) | p_spdu[3];
        p_sessions[i_session_id - 1].pf_handle( p_access, i_session_id,
                                                       p_spdu + 4, i_size - 4 );
        break;

    case ST_OPEN_SESSION_REQUEST:
        if ( i_size != 6 || p_spdu[1] != 0x4 )
            return;
        SessionOpen( p_access, i_slot, p_spdu, i_size );
        break;

    case ST_CREATE_SESSION_RESPONSE:
        if ( i_size != 9 || p_spdu[1] != 0x7 )
            return;
        SessionCreateResponse( p_access, i_slot, p_spdu, i_size );
        break;

    case ST_CLOSE_SESSION_REQUEST:
        if ( i_size != 4 || p_spdu[1] != 0x2 )
            return;
        i_session_id = ((int)p_spdu[2] << 8) | p_spdu[3];
        SessionClose( p_access, i_session_id );
        break;

    case ST_CLOSE_SESSION_RESPONSE:
        if ( i_size != 5 || p_spdu[1] != 0x3 )
            return;
        i_session_id = ((int)p_spdu[3] << 8) | p_spdu[4];
        if ( p_spdu[2] )
        {
            msg_Err( p_access, "closing a session which is not allocated (%d)",
                     i_session_id );
        }
        else
        {
            if ( p_sessions[i_session_id - 1].pf_close != NULL )
                p_sessions[i_session_id - 1].pf_close( p_access,
                                                              i_session_id );
            p_sessions[i_session_id - 1].i_resource_id = 0;
        }
        break;

    default:
        msg_Err( p_access, "unexpected tag in SPDUHandle (%x)", p_spdu[0] );
        break;
    }
}


/*
 * Application layer
 */

#define AOT_NONE                    0x000000
#define AOT_PROFILE_ENQ             0x9F8010
#define AOT_PROFILE                 0x9F8011
#define AOT_PROFILE_CHANGE          0x9F8012
#define AOT_APPLICATION_INFO_ENQ    0x9F8020
#define AOT_APPLICATION_INFO        0x9F8021
#define AOT_ENTER_MENU              0x9F8022
#define AOT_CA_INFO_ENQ             0x9F8030
#define AOT_CA_INFO                 0x9F8031
#define AOT_CA_PMT                  0x9F8032
#define AOT_CA_PMT_REPLY            0x9F8033
#define AOT_TUNE                    0x9F8400
#define AOT_REPLACE                 0x9F8401
#define AOT_CLEAR_REPLACE           0x9F8402
#define AOT_ASK_RELEASE             0x9F8403
#define AOT_DATE_TIME_ENQ           0x9F8440
#define AOT_DATE_TIME               0x9F8441
#define AOT_CLOSE_MMI               0x9F8800
#define AOT_DISPLAY_CONTROL         0x9F8801
#define AOT_DISPLAY_REPLY           0x9F8802
#define AOT_TEXT_LAST               0x9F8803
#define AOT_TEXT_MORE               0x9F8804
#define AOT_KEYPAD_CONTROL          0x9F8805
#define AOT_KEYPRESS                0x9F8806
#define AOT_ENQ                     0x9F8807
#define AOT_ANSW                    0x9F8808
#define AOT_MENU_LAST               0x9F8809
#define AOT_MENU_MORE               0x9F880A
#define AOT_MENU_ANSW               0x9F880B
#define AOT_LIST_LAST               0x9F880C
#define AOT_LIST_MORE               0x9F880D
#define AOT_SUBTITLE_SEGMENT_LAST   0x9F880E
#define AOT_SUBTITLE_SEGMENT_MORE   0x9F880F
#define AOT_DISPLAY_MESSAGE         0x9F8810
#define AOT_SCENE_END_MARK          0x9F8811
#define AOT_SCENE_DONE              0x9F8812
#define AOT_SCENE_CONTROL           0x9F8813
#define AOT_SUBTITLE_DOWNLOAD_LAST  0x9F8814
#define AOT_SUBTITLE_DOWNLOAD_MORE  0x9F8815
#define AOT_FLUSH_DOWNLOAD          0x9F8816
#define AOT_DOWNLOAD_REPLY          0x9F8817
#define AOT_COMMS_CMD               0x9F8C00
#define AOT_CONNECTION_DESCRIPTOR   0x9F8C01
#define AOT_COMMS_REPLY             0x9F8C02
#define AOT_COMMS_SEND_LAST         0x9F8C03
#define AOT_COMMS_SEND_MORE         0x9F8C04
#define AOT_COMMS_RCV_LAST          0x9F8C05
#define AOT_COMMS_RCV_MORE          0x9F8C06

/*****************************************************************************
 * APDUGetTag
 *****************************************************************************/
static int APDUGetTag( const uint8_t *p_apdu, int i_size )
{
    if ( i_size >= 3 )
    {
        int i, t = 0;
        for ( i = 0; i < 3; i++ )
            t = (t << 8) | *p_apdu++;
        return t;
    }

    return AOT_NONE;
}

/*****************************************************************************
 * APDUGetLength
 *****************************************************************************/
static uint8_t *APDUGetLength( uint8_t *p_apdu, int *pi_size )
{
    return GetLength( &p_apdu[3], pi_size );
}

/*****************************************************************************
 * APDUSend
 *****************************************************************************/
static int APDUSend( access_t * p_access, int i_session_id, int i_tag,
                     uint8_t *p_data, int i_size )
{
    uint8_t *p_apdu = malloc( i_size + 12 );
    uint8_t *p = p_apdu;
    ca_msg_t ca_msg;
    int i_ret;

    *p++ = (i_tag >> 16);
    *p++ = (i_tag >> 8) & 0xff;
    *p++ = i_tag & 0xff;
    p = SetLength( p, i_size );
    if ( i_size )
        memcpy( p, p_data, i_size );
    if ( i_ca_type == CA_CI_LINK )
    {
        i_ret = SPDUSend( p_access, i_session_id, p_apdu, i_size + p - p_apdu );
    }
    else
    {
        if ( i_size + p - p_apdu > 256 )
        {
            msg_Err( p_access, "CAM: apdu overflow" );
            i_ret = -1;
        }
        else
        {
            ca_msg.length = i_size + p - p_apdu;
            if ( i_size == 0 ) ca_msg.length=3;
            memcpy( ca_msg.msg, p_apdu, i_size + p - p_apdu );
            i_ret = ioctl(i_ca_handle, CA_SEND_MSG, &ca_msg );
            if ( i_ret < 0 )
            {
                msg_Err( p_access, "Error sending to CAM: %m" );
                i_ret = -1;
            }
        }
    }
    free( p_apdu );
    return i_ret;
}

/*
 * Resource Manager
 */

/*****************************************************************************
 * ResourceManagerHandle
 *****************************************************************************/
static void ResourceManagerHandle( access_t * p_access, int i_session_id,
                                   uint8_t *p_apdu, int i_size )
{
    int i_tag = APDUGetTag( p_apdu, i_size );

    switch ( i_tag )
    {
    case AOT_PROFILE_ENQ:
    {
        int resources[] = { htonl(RI_RESOURCE_MANAGER),
                            htonl(RI_APPLICATION_INFORMATION),
                            htonl(RI_CONDITIONAL_ACCESS_SUPPORT),
                            htonl(RI_DATE_TIME),
                            htonl(RI_MMI)
                          };
        APDUSend( p_access, i_session_id, AOT_PROFILE, (uint8_t*)resources,
                  sizeof(resources) );
        break;
    }
    case AOT_PROFILE:
        APDUSend( p_access, i_session_id, AOT_PROFILE_CHANGE, NULL, 0 );
        break;

    default:
        msg_Err( p_access, "unexpected tag in ResourceManagerHandle (0x%x)",
                 i_tag );
    }
}

/*****************************************************************************
 * ResourceManagerOpen
 *****************************************************************************/
static void ResourceManagerOpen( access_t * p_access, int i_session_id )
{

    msg_Dbg( p_access, "opening ResourceManager session (%d)", i_session_id );

    p_sessions[i_session_id - 1].pf_handle = ResourceManagerHandle;

    APDUSend( p_access, i_session_id, AOT_PROFILE_ENQ, NULL, 0 );
}

/*
 * Application Information
 */

/*****************************************************************************
 * ApplicationInformationEnterMenu
 *****************************************************************************/
static void ApplicationInformationEnterMenu( access_t * p_access,
                                             int i_session_id )
{
    int i_slot = p_sessions[i_session_id - 1].i_slot;

    msg_Dbg( p_access, "entering MMI menus on session %d", i_session_id );
    APDUSend( p_access, i_session_id, AOT_ENTER_MENU, NULL, 0 );
    pb_slot_mmi_expected[i_slot] = true;
}

/*****************************************************************************
 * ApplicationInformationHandle
 *****************************************************************************/
static void ApplicationInformationHandle( access_t * p_access, int i_session_id,
                                          uint8_t *p_apdu, int i_size )
{
    int i_tag = APDUGetTag( p_apdu, i_size );

    switch ( i_tag )
    {
    case AOT_APPLICATION_INFO:
    {
        int i_type, i_manufacturer, i_code;
        int l = 0;
        uint8_t *d = APDUGetLength( p_apdu, &l );

        if ( l < 4 ) break;
        p_apdu[l + 4] = '\0';

        i_type = *d++;
        i_manufacturer = ((int)d[0] << 8) | d[1];
        d += 2;
        i_code = ((int)d[0] << 8) | d[1];
        d += 2;
        d = GetLength( d, &l );
        d[l] = '\0';
        msg_Info( p_access, "CAM: %s, %02X, %04X, %04X",
                  d, i_type, i_manufacturer, i_code );
        break;
    }
    default:
        msg_Err( p_access,
                 "unexpected tag in ApplicationInformationHandle (0x%x)",
                 i_tag );
    }
}

/*****************************************************************************
 * ApplicationInformationOpen
 *****************************************************************************/
static void ApplicationInformationOpen( access_t * p_access, int i_session_id )
{

    msg_Dbg( p_access, "opening ApplicationInformation session (%d)", i_session_id );

    p_sessions[i_session_id - 1].pf_handle = ApplicationInformationHandle;

    APDUSend( p_access, i_session_id, AOT_APPLICATION_INFO_ENQ, NULL, 0 );
}

/*
 * Conditional Access
 */

#define MAX_CASYSTEM_IDS 64

typedef struct
{
    uint16_t pi_system_ids[MAX_CASYSTEM_IDS + 1];

    int i_selected_programs;
    int b_high_level;
} system_ids_t;

static bool CheckSystemID( system_ids_t *p_ids, uint16_t i_id )
{
    int i = 0;
    if( !p_ids ) return false;
    if( p_ids->b_high_level ) return true;

    while ( p_ids->pi_system_ids[i] )
    {
        if ( p_ids->pi_system_ids[i] == i_id )
            return true;
        i++;
    }

    return false;
}

/*****************************************************************************
 * CAPMTBuild
 *****************************************************************************/
static int GetCADSize( system_ids_t *p_ids, dvbpsi_descriptor_t *p_dr )
{
    int i_cad_size = 0;

    while ( p_dr != NULL )
    {
        if( p_dr->i_tag == 0x9 )
        {
            uint16_t i_sysid = ((uint16_t)p_dr->p_data[0] << 8)
                                    | p_dr->p_data[1];
            if ( CheckSystemID( p_ids, i_sysid ) )
                i_cad_size += p_dr->i_length + 2;
        }
        p_dr = p_dr->p_next;
    }

    return i_cad_size;
}

static uint8_t *CAPMTHeader( system_ids_t *p_ids, uint8_t i_list_mgt,
                             uint16_t i_program_number, uint8_t i_version,
                             int i_size, dvbpsi_descriptor_t *p_dr,
                             uint8_t i_cmd )
{
    uint8_t *p_data;

    if ( i_size )
        p_data = malloc( 7 + i_size );
    else
        p_data = malloc( 6 );

    p_data[0] = i_list_mgt;
    p_data[1] = i_program_number >> 8;
    p_data[2] = i_program_number & 0xff;
    p_data[3] = ((i_version & 0x1f) << 1) | 0x1;

    if ( i_size )
    {
        int i;

        p_data[4] = (i_size + 1) >> 8;
        p_data[5] = (i_size + 1) & 0xff;
        p_data[6] = i_cmd;
        i = 7;

        while ( p_dr != NULL )
        {
            if( p_dr->i_tag == 0x9 )
            {
                uint16_t i_sysid = ((uint16_t)p_dr->p_data[0] << 8)
                                    | p_dr->p_data[1];
                if ( CheckSystemID( p_ids, i_sysid ) )
                {
                    p_data[i] = 0x9;
                    p_data[i + 1] = p_dr->i_length;
                    memcpy( &p_data[i + 2], p_dr->p_data, p_dr->i_length );
//                    p_data[i+4] &= 0x1f;
                    i += p_dr->i_length + 2;
                }
            }
            p_dr = p_dr->p_next;
        }
    }
    else
    {
        p_data[4] = 0;
        p_data[5] = 0;
    }

    return p_data;
}

static uint8_t *CAPMTES( system_ids_t *p_ids, uint8_t *p_capmt,
                         int i_capmt_size, uint8_t i_type, uint16_t i_pid,
                         int i_size, dvbpsi_descriptor_t *p_dr,
                         uint8_t i_cmd )
{
    uint8_t *p_data;
    int i;

    if ( i_size )
        p_data = realloc( p_capmt, i_capmt_size + 6 + i_size );
    else
        p_data = realloc( p_capmt, i_capmt_size + 5 );

    i = i_capmt_size;

    p_data[i] = i_type;
    p_data[i + 1] = i_pid >> 8;
    p_data[i + 2] = i_pid & 0xff;

    if ( i_size )
    {
        p_data[i + 3] = (i_size + 1) >> 8;
        p_data[i + 4] = (i_size + 1) & 0xff;
        p_data[i + 5] = i_cmd;
        i += 6;

        while ( p_dr != NULL )
        {
            if( p_dr->i_tag == 0x9 )
            {
                uint16_t i_sysid = ((uint16_t)p_dr->p_data[0] << 8)
                                    | p_dr->p_data[1];
                if ( CheckSystemID( p_ids, i_sysid ) )
                {
                    p_data[i] = 0x9;
                    p_data[i + 1] = p_dr->i_length;
                    memcpy( &p_data[i + 2], p_dr->p_data, p_dr->i_length );
                    i += p_dr->i_length + 2;
                }
            }
            p_dr = p_dr->p_next;
        }
    }
    else
    {
        p_data[i + 3] = 0;
        p_data[i + 4] = 0;
    }

    return p_data;
}

static uint8_t *CAPMTBuild( access_t * p_access, int i_session_id,
                            dvbpsi_pmt_t *p_pmt, uint8_t i_list_mgt,
                            uint8_t i_cmd, int *pi_capmt_size )
{
    system_ids_t *p_ids =
        (system_ids_t *)p_sessions[i_session_id - 1].p_sys;
    dvbpsi_pmt_es_t *p_es;
    int i_cad_size, i_cad_program_size;
    uint8_t *p_capmt;

    i_cad_size = i_cad_program_size =
            GetCADSize( p_ids, p_pmt->p_first_descriptor );
    for( p_es = p_pmt->p_first_es; p_es != NULL; p_es = p_es->p_next )
    {
        i_cad_size += GetCADSize( p_ids, p_es->p_first_descriptor );
    }

    if ( !i_cad_size )
    {
        msg_Warn( p_access,
                  "no compatible scrambling system for SID %d on session %d",
                  p_pmt->i_program_number, i_session_id );
        *pi_capmt_size = 0;
        return NULL;
    }

    p_capmt = CAPMTHeader( p_ids, i_list_mgt, p_pmt->i_program_number,
                           p_pmt->i_version, i_cad_program_size,
                           p_pmt->p_first_descriptor, i_cmd );

    if ( i_cad_program_size )
        *pi_capmt_size = 7 + i_cad_program_size;
    else
        *pi_capmt_size = 6;

    for( p_es = p_pmt->p_first_es; p_es != NULL; p_es = p_es->p_next )
    {
        i_cad_size = GetCADSize( p_ids, p_es->p_first_descriptor );

        if ( i_cad_size || i_cad_program_size )
        {
            p_capmt = CAPMTES( p_ids, p_capmt, *pi_capmt_size, p_es->i_type,
                               p_es->i_pid, i_cad_size,
                               p_es->p_first_descriptor, i_cmd );
            if ( i_cad_size )
                *pi_capmt_size += 6 + i_cad_size;
            else
                *pi_capmt_size += 5;
        }
    }

    if ( *pi_capmt_size <= 7 + i_cad_program_size )
    {
        msg_Dbg( p_access, "CAPMT not needed, no ES selected" );
        free( p_capmt );
        *pi_capmt_size = 0;
        return NULL;
    }

    return p_capmt;
}

/*****************************************************************************
 * CAPMTFirst
 *****************************************************************************/
static void CAPMTFirst( access_t * p_access, int i_session_id,
                        dvbpsi_pmt_t *p_pmt )
{
    uint8_t *p_capmt;
    int i_capmt_size;

    msg_Dbg( p_access, "adding first CAPMT for SID %d on session %d",
             p_pmt->i_program_number, i_session_id );

    p_capmt = CAPMTBuild( p_access, i_session_id, p_pmt,
                          0x3 /* only */, 0x1 /* ok_descrambling */,
                          &i_capmt_size );

    if ( i_capmt_size )
    {
        APDUSend( p_access, i_session_id, AOT_CA_PMT, p_capmt, i_capmt_size );
        free( p_capmt );
    }
}

/*****************************************************************************
 * CAPMTAdd
 *****************************************************************************/
static void CAPMTAdd( access_t * p_access, int i_session_id,
                      dvbpsi_pmt_t *p_pmt )
{
    system_ids_t *p_ids =
        (system_ids_t *)p_sessions[i_session_id - 1].p_sys;
    uint8_t *p_capmt;
    int i_capmt_size;

    if( p_ids->i_selected_programs >= CAM_PROG_MAX )
    {
        msg_Warn( p_access, "Not adding CAPMT for SID %d, too many programs",
                  p_pmt->i_program_number );
        return;
    }
    p_ids->i_selected_programs++;
    if( p_ids->i_selected_programs == 1 )
    {
        CAPMTFirst( p_access, i_session_id, p_pmt );
        return;
    }

    if( b_slow_cam )
        msleep( CAPMT_WAIT * 1000 );

    msg_Dbg( p_access, "adding CAPMT for SID %d on session %d",
             p_pmt->i_program_number, i_session_id );

    p_capmt = CAPMTBuild( p_access, i_session_id, p_pmt,
                          0x4 /* add */, 0x1 /* ok_descrambling */,
                          &i_capmt_size );

    if ( i_capmt_size )
    {
        APDUSend( p_access, i_session_id, AOT_CA_PMT, p_capmt, i_capmt_size );
        free( p_capmt );
    }
}

/*****************************************************************************
 * CAPMTUpdate
 *****************************************************************************/
static void CAPMTUpdate( access_t * p_access, int i_session_id,
                         dvbpsi_pmt_t *p_pmt )
{
    uint8_t *p_capmt;
    int i_capmt_size;

    msg_Dbg( p_access, "updating CAPMT for SID %d on session %d",
             p_pmt->i_program_number, i_session_id );

    p_capmt = CAPMTBuild( p_access, i_session_id, p_pmt,
                          0x5 /* update */, 0x1 /* ok_descrambling */,
                          &i_capmt_size );

    if ( i_capmt_size )
    {
        APDUSend( p_access, i_session_id, AOT_CA_PMT, p_capmt, i_capmt_size );
        free( p_capmt );
    }
}

/*****************************************************************************
 * CAPMTDelete
 *****************************************************************************/
static void CAPMTDelete( access_t * p_access, int i_session_id,
                         dvbpsi_pmt_t *p_pmt )
{
    system_ids_t *p_ids =
        (system_ids_t *)p_sessions[i_session_id - 1].p_sys;
    uint8_t *p_capmt;
    int i_capmt_size;

    p_ids->i_selected_programs--;
    msg_Dbg( p_access, "deleting CAPMT for SID %d on session %d",
             p_pmt->i_program_number, i_session_id );

    p_capmt = CAPMTBuild( p_access, i_session_id, p_pmt,
                          0x5 /* update */, 0x4 /* not selected */,
                          &i_capmt_size );

    if ( i_capmt_size )
    {
        APDUSend( p_access, i_session_id, AOT_CA_PMT, p_capmt, i_capmt_size );
        free( p_capmt );
    }
}

/*****************************************************************************
 * ConditionalAccessHandle
 *****************************************************************************/
static void ConditionalAccessHandle( access_t * p_access, int i_session_id,
                                     uint8_t *p_apdu, int i_size )
{
    system_ids_t *p_ids =
        (system_ids_t *)p_sessions[i_session_id - 1].p_sys;
    int i_tag = APDUGetTag( p_apdu, i_size );

    switch ( i_tag )
    {
    case AOT_CA_INFO:
    {
        int i;
        int l = 0;
        uint8_t *d = APDUGetLength( p_apdu, &l );
        msg_Dbg( p_access, "CA system IDs supported by the application :" );

        for ( i = 0; i < l / 2; i++ )
        {
            p_ids->pi_system_ids[i] = ((uint16_t)d[0] << 8) | d[1];
            d += 2;
            msg_Dbg( p_access, "- 0x%x", p_ids->pi_system_ids[i] );
        }
        p_ids->pi_system_ids[i] = 0;

        /* TODO Send PMT to CAM
        demux_ResendCAPMTs();
        */
        break;
    }

    default:
        msg_Err( p_access,
                 "unexpected tag in ConditionalAccessHandle (0x%x)",
                 i_tag );
    }
}

/*****************************************************************************
 * ConditionalAccessClose
 *****************************************************************************/
static void ConditionalAccessClose( access_t * p_access, int i_session_id )
{

    msg_Dbg( p_access, "closing ConditionalAccess session (%d)", i_session_id );

    free( p_sessions[i_session_id - 1].p_sys );
}

/*****************************************************************************
 * ConditionalAccessOpen
 *****************************************************************************/
static void ConditionalAccessOpen( access_t * p_access, int i_session_id )
{

    msg_Dbg( p_access, "opening ConditionalAccess session (%d)", i_session_id );

    p_sessions[i_session_id - 1].pf_handle = ConditionalAccessHandle;
    p_sessions[i_session_id - 1].pf_close = ConditionalAccessClose;
    p_sessions[i_session_id - 1].p_sys = malloc(sizeof(system_ids_t));
    memset( p_sessions[i_session_id - 1].p_sys, 0,
            sizeof(system_ids_t) );

    APDUSend( p_access, i_session_id, AOT_CA_INFO_ENQ, NULL, 0 );
}

/*
 * Date Time
 */

typedef struct
{
    int i_interval;
    mtime_t i_last;
} date_time_t;

/*****************************************************************************
 * DateTimeSend
 *****************************************************************************/
static void DateTimeSend( access_t * p_access, int i_session_id )
{
    date_time_t *p_date =
        (date_time_t *)p_sessions[i_session_id - 1].p_sys;

    time_t t = time(NULL);
    struct tm tm_gmt;
    struct tm tm_loc;

    if ( gmtime_r(&t, &tm_gmt) && localtime_r(&t, &tm_loc) )
    {
        int Y = tm_gmt.tm_year;
        int M = tm_gmt.tm_mon + 1;
        int D = tm_gmt.tm_mday;
        int L = (M == 1 || M == 2) ? 1 : 0;
        int MJD = 14956 + D + (int)((Y - L) * 365.25)
                    + (int)((M + 1 + L * 12) * 30.6001);
        uint8_t p_response[7];

#define DEC2BCD(d) (((d / 10) << 4) + (d % 10))

        p_response[0] = htons(MJD) >> 8;
        p_response[1] = htons(MJD) & 0xff;
        p_response[2] = DEC2BCD(tm_gmt.tm_hour);
        p_response[3] = DEC2BCD(tm_gmt.tm_min);
        p_response[4] = DEC2BCD(tm_gmt.tm_sec);
        p_response[5] = htons(tm_loc.tm_gmtoff / 60) >> 8;
        p_response[6] = htons(tm_loc.tm_gmtoff / 60) & 0xff;

        APDUSend( p_access, i_session_id, AOT_DATE_TIME, p_response, 7 );

        p_date->i_last = mdate();
    }
}

/*****************************************************************************
 * DateTimeHandle
 *****************************************************************************/
static void DateTimeHandle( access_t * p_access, int i_session_id,
                            uint8_t *p_apdu, int i_size )
{
    date_time_t *p_date =
        (date_time_t *)p_sessions[i_session_id - 1].p_sys;

    int i_tag = APDUGetTag( p_apdu, i_size );

    switch ( i_tag )
    {
    case AOT_DATE_TIME_ENQ:
    {
        int l;
        const uint8_t *d = APDUGetLength( p_apdu, &l );

        if ( l > 0 )
        {
            p_date->i_interval = *d;
            msg_Dbg( p_access, "DateTimeHandle : interval set to %d",
                     p_date->i_interval );
        }
        else
            p_date->i_interval = 0;

        DateTimeSend( p_access, i_session_id );
        break;
    }
    default:
        msg_Err( p_access, "unexpected tag in DateTimeHandle (0x%x)", i_tag );
    }
}

/*****************************************************************************
 * DateTimeManage
 *****************************************************************************/
static void DateTimeManage( access_t * p_access, int i_session_id )
{
    date_time_t *p_date =
        (date_time_t *)p_sessions[i_session_id - 1].p_sys;

    if ( p_date->i_interval
          && mdate() > p_date->i_last + (mtime_t)p_date->i_interval * 1000000 )
    {
        DateTimeSend( p_access, i_session_id );
    }
}

/*****************************************************************************
 * DateTimeClose
 *****************************************************************************/
static void DateTimeClose( access_t * p_access, int i_session_id )
{
    msg_Dbg( p_access, "closing DateTime session (%d)", i_session_id );

    free( p_sessions[i_session_id - 1].p_sys );
}

/*****************************************************************************
 * DateTimeOpen
 *****************************************************************************/
static void DateTimeOpen( access_t * p_access, int i_session_id )
{
    msg_Dbg( p_access, "opening DateTime session (%d)", i_session_id );

    p_sessions[i_session_id - 1].pf_handle = DateTimeHandle;
    p_sessions[i_session_id - 1].pf_manage = DateTimeManage;
    p_sessions[i_session_id - 1].pf_close = DateTimeClose;
    p_sessions[i_session_id - 1].p_sys = malloc(sizeof(date_time_t));
    memset( p_sessions[i_session_id - 1].p_sys, 0, sizeof(date_time_t) );

    DateTimeSend( p_access, i_session_id );
}

/*
 * MMI
 */

/* Display Control Commands */

#define DCC_SET_MMI_MODE                          0x01
#define DCC_DISPLAY_CHARACTER_TABLE_LIST          0x02
#define DCC_INPUT_CHARACTER_TABLE_LIST            0x03
#define DCC_OVERLAY_GRAPHICS_CHARACTERISTICS      0x04
#define DCC_FULL_SCREEN_GRAPHICS_CHARACTERISTICS  0x05

/* MMI Modes */

#define MM_HIGH_LEVEL                      0x01
#define MM_LOW_LEVEL_OVERLAY_GRAPHICS      0x02
#define MM_LOW_LEVEL_FULL_SCREEN_GRAPHICS  0x03

/* Display Reply IDs */

#define DRI_MMI_MODE_ACK                              0x01
#define DRI_LIST_DISPLAY_CHARACTER_TABLES             0x02
#define DRI_LIST_INPUT_CHARACTER_TABLES               0x03
#define DRI_LIST_GRAPHIC_OVERLAY_CHARACTERISTICS      0x04
#define DRI_LIST_FULL_SCREEN_GRAPHIC_CHARACTERISTICS  0x05
#define DRI_UNKNOWN_DISPLAY_CONTROL_CMD               0xF0
#define DRI_UNKNOWN_MMI_MODE                          0xF1
#define DRI_UNKNOWN_CHARACTER_TABLE                   0xF2

/* Enquiry Flags */

#define EF_BLIND  0x01

/* Answer IDs */

#define AI_CANCEL  0x00
#define AI_ANSWER  0x01

typedef struct
{
    en50221_mmi_object_t last_object;
} mmi_t;

static inline void en50221_MMIFree( en50221_mmi_object_t *p_object )
{
    int i;

    switch ( p_object->i_object_type )
    {
    case EN50221_MMI_ENQ:
        free( p_object->u.enq.psz_text );
        break;

    case EN50221_MMI_ANSW:
        if ( p_object->u.answ.b_ok )
        {
            free( p_object->u.answ.psz_answ );
        }
        break;

    case EN50221_MMI_MENU:
    case EN50221_MMI_LIST:
        free( p_object->u.menu.psz_title );
        free( p_object->u.menu.psz_subtitle );
        free( p_object->u.menu.psz_bottom );
        for ( i = 0; i < p_object->u.menu.i_choices; i++ )
        {
            free( p_object->u.menu.ppsz_choices[i] );
        }
        free( p_object->u.menu.ppsz_choices );
        break;

    default:
        break;
    }
}

/*****************************************************************************
 * MMISendObject
 *****************************************************************************/
static void MMISendObject( access_t *p_access, int i_session_id,
                           en50221_mmi_object_t *p_object )
{
    int i_slot = p_sessions[i_session_id - 1].i_slot;
    uint8_t *p_data;
    int i_size, i_tag;

    switch ( p_object->i_object_type )
    {
    case EN50221_MMI_ANSW:
        i_tag = AOT_ANSW;
        i_size = 1 + strlen( p_object->u.answ.psz_answ );
        p_data = malloc( i_size );
        p_data[0] = (p_object->u.answ.b_ok == true) ? 0x1 : 0x0;
        strncpy( (char *)&p_data[1], p_object->u.answ.psz_answ, i_size - 1 );
        break;

    case EN50221_MMI_MENU_ANSW:
        i_tag = AOT_MENU_ANSW;
        i_size = 1;
        p_data = malloc( i_size );
        p_data[0] = p_object->u.menu_answ.i_choice;
        break;

    default:
        msg_Err( p_access, "unknown MMI object %d", p_object->i_object_type );
        return;
    }

    APDUSend( p_access, i_session_id, i_tag, p_data, i_size );
    free( p_data );

    pb_slot_mmi_expected[i_slot] = true;
}

/*****************************************************************************
 * MMISendClose
 *****************************************************************************/
static void MMISendClose( access_t *p_access, int i_session_id )
{
    int i_slot = p_sessions[i_session_id - 1].i_slot;

    APDUSend( p_access, i_session_id, AOT_CLOSE_MMI, NULL, 0 );

    pb_slot_mmi_expected[i_slot] = true;
}

/*****************************************************************************
 * MMIDisplayReply
 *****************************************************************************/
static void MMIDisplayReply( access_t *p_access, int i_session_id )
{
    uint8_t p_response[2];

    p_response[0] = DRI_MMI_MODE_ACK;
    p_response[1] = MM_HIGH_LEVEL;

    APDUSend( p_access, i_session_id, AOT_DISPLAY_REPLY, p_response, 2 );

    msg_Dbg( p_access, "sending DisplayReply on session (%d)", i_session_id );
}

/*****************************************************************************
 * MMIGetText
 *****************************************************************************/
static char *MMIGetText( access_t *p_access, uint8_t **pp_apdu, int *pi_size )
{
    int i_tag = APDUGetTag( *pp_apdu, *pi_size );
    char *psz_text;
    int l;
    uint8_t *d;

    if ( i_tag != AOT_TEXT_LAST )
    {
        msg_Err( p_access, "unexpected text tag: %06x", i_tag );
        *pi_size = 0;
        return strdup( "" );
    }

    d = APDUGetLength( *pp_apdu, &l );
    psz_text = malloc( l + 1 );
    strncpy( psz_text, (char *)d, l );
    psz_text[l] = '\0';

    *pp_apdu += l + 4;
    *pi_size -= l + 4;

    return psz_text;
}

/*****************************************************************************
 * MMIHandleEnq
 *****************************************************************************/
static void MMIHandleEnq( access_t *p_access, int i_session_id,
                          uint8_t *p_apdu, int i_size )
{
    mmi_t *p_mmi = (mmi_t *)p_sessions[i_session_id - 1].p_sys;
    int i_slot = p_sessions[i_session_id - 1].i_slot;
    int l;
    uint8_t *d = APDUGetLength( p_apdu, &l );

    en50221_MMIFree( &p_mmi->last_object );
    p_mmi->last_object.i_object_type = EN50221_MMI_ENQ;
    p_mmi->last_object.u.enq.b_blind = (*d & 0x1) ? true : false;
    d += 2; /* skip answer_text_length because it is not mandatory */
    l -= 2;
    p_mmi->last_object.u.enq.psz_text = malloc( l + 1 );
    strncpy( p_mmi->last_object.u.enq.psz_text, (char *)d, l );
    p_mmi->last_object.u.enq.psz_text[l] = '\0';

    msg_Dbg( p_access, "MMI enq: %s%s", p_mmi->last_object.u.enq.psz_text,
             p_mmi->last_object.u.enq.b_blind == true ? " (blind)" : "" );
    pb_slot_mmi_expected[i_slot] = false;
    pb_slot_mmi_undisplayed[i_slot] = true;
}

/*****************************************************************************
 * MMIHandleMenu
 *****************************************************************************/
static void MMIHandleMenu( access_t *p_access, int i_session_id, int i_tag,
                           uint8_t *p_apdu, int i_size )
{
    mmi_t *p_mmi = (mmi_t *)p_sessions[i_session_id - 1].p_sys;
    int i_slot = p_sessions[i_session_id - 1].i_slot;
    int l;
    uint8_t *d = APDUGetLength( p_apdu, &l );

    en50221_MMIFree( &p_mmi->last_object );
    p_mmi->last_object.i_object_type = (i_tag == AOT_MENU_LAST) ?
                                       EN50221_MMI_MENU : EN50221_MMI_LIST;
    p_mmi->last_object.u.menu.i_choices = 0;
    p_mmi->last_object.u.menu.ppsz_choices = NULL;

    if ( l > 0 )
    {
        l--; d++; /* choice_nb */

#define GET_FIELD( x )                                                      \
        if ( l > 0 )                                                        \
        {                                                                   \
            p_mmi->last_object.u.menu.psz_##x                               \
                            = MMIGetText( p_access, &d, &l );               \
            msg_Dbg( p_access, "MMI " STRINGIFY( x ) ": %s",                \
                     p_mmi->last_object.u.menu.psz_##x );                   \
        }

        GET_FIELD( title );
        GET_FIELD( subtitle );
        GET_FIELD( bottom );
#undef GET_FIELD

        while ( l > 0 )
        {
            char *psz_text = MMIGetText( p_access, &d, &l );
            TAB_APPEND( p_mmi->last_object.u.menu.i_choices,
                        p_mmi->last_object.u.menu.ppsz_choices,
                        psz_text );
            msg_Dbg( p_access, "MMI choice: %s", psz_text );
        }
    }
    pb_slot_mmi_expected[i_slot] = false;
    pb_slot_mmi_undisplayed[i_slot] = true;
}

/*****************************************************************************
 * MMIHandle
 *****************************************************************************/
static void MMIHandle( access_t *p_access, int i_session_id,
                       uint8_t *p_apdu, int i_size )
{
    int i_tag = APDUGetTag( p_apdu, i_size );

    switch ( i_tag )
    {
    case AOT_DISPLAY_CONTROL:
    {
        int l;
        uint8_t *d = APDUGetLength( p_apdu, &l );

        if ( l > 0 )
        {
            switch ( *d )
            {
            case DCC_SET_MMI_MODE:
                if ( l == 2 && d[1] == MM_HIGH_LEVEL )
                    MMIDisplayReply( p_access, i_session_id );
                else
                    msg_Err( p_access, "unsupported MMI mode %02x", d[1] );
                break;

            default:
                msg_Err( p_access, "unsupported display control command %02x",
                         *d );
                break;
            }
        }
        break;
    }

    case AOT_ENQ:
        MMIHandleEnq( p_access, i_session_id, p_apdu, i_size );
        break;

    case AOT_LIST_LAST:
    case AOT_MENU_LAST:
        MMIHandleMenu( p_access, i_session_id, i_tag, p_apdu, i_size );
        break;

    case AOT_CLOSE_MMI:
        SessionSendClose( p_access, i_session_id );
        break;

    default:
        msg_Err( p_access, "unexpected tag in MMIHandle (0x%x)", i_tag );
    }
}

/*****************************************************************************
 * MMIClose
 *****************************************************************************/
static void MMIClose( access_t *p_access, int i_session_id )
{
    int i_slot = p_sessions[i_session_id - 1].i_slot;
    mmi_t *p_mmi = (mmi_t *)p_sessions[i_session_id - 1].p_sys;

    en50221_MMIFree( &p_mmi->last_object );
    free( p_sessions[i_session_id - 1].p_sys );

    msg_Dbg( p_access, "closing MMI session (%d)", i_session_id );
    pb_slot_mmi_expected[i_slot] = false;
    pb_slot_mmi_undisplayed[i_slot] = true;
}

/*****************************************************************************
 * MMIOpen
 *****************************************************************************/
static void MMIOpen( access_t *p_access, int i_session_id )
{
    mmi_t *p_mmi;

    msg_Dbg( p_access, "opening MMI session (%d)", i_session_id );

    p_sessions[i_session_id - 1].pf_handle = MMIHandle;
    p_sessions[i_session_id - 1].pf_close = MMIClose;
    p_sessions[i_session_id - 1].p_sys = malloc(sizeof(mmi_t));
    p_mmi = (mmi_t *)p_sessions[i_session_id - 1].p_sys;
    p_mmi->last_object.i_object_type = EN50221_MMI_NONE;
}


/*
 * Hardware handling
 */

/*****************************************************************************
 * InitSlot: Open the transport layer
 *****************************************************************************/
#define MAX_TC_RETRIES 5

static int InitSlot( access_t * p_access, int i_slot )
{
    int i;

    if ( TPDUSend( p_access, i_slot, T_CREATE_TC, NULL, 0 )
            != 0 )
    {
        msg_Err( p_access, "en50221_Init: couldn't send TPDU on slot %d",
                 i_slot );
        return -1;
    }

    /* Wait for T_CTC_REPLY */
    for ( i = 0; i < MAX_TC_RETRIES; i++ )
    {
        uint8_t i_tag;
        if ( TPDURecv( p_access, i_slot, &i_tag, NULL, NULL ) == 0
              && i_tag == T_CTC_REPLY )
        {
            pb_active_slot[i_slot] = true;
            break;
        }
    }

    if ( pb_active_slot[i_slot] )
    {
        i_ca_timeout = 100000;
        return 0;
    }

    return -1;
}

/*****************************************************************************
 * ResetSlot
 *****************************************************************************/
static void ResetSlot( int i_slot )
{
    int i_session_id;

    if ( ioctl( i_ca_handle, CA_RESET, 1 << i_slot ) != 0 )
        msg_Err( NULL, "en50221_Poll: couldn't reset slot %d", i_slot );
    pb_active_slot[i_slot] = false;
    pb_tc_has_data[i_slot] = false;

    /* Close all sessions for this slot. */
    for ( i_session_id = 1; i_session_id <= MAX_SESSIONS; i_session_id++ )
    {
        if ( p_sessions[i_session_id - 1].i_resource_id
              && p_sessions[i_session_id - 1].i_slot == i_slot )
        {
            if ( p_sessions[i_session_id - 1].pf_close != NULL )
            {
                p_sessions[i_session_id - 1].pf_close( NULL, i_session_id );
            }
            p_sessions[i_session_id - 1].i_resource_id = 0;
        }
    }

    i_ca_timeout = 100000;
}


/*
 * External entry points
 */

/*****************************************************************************
 * en50221_Init : Initialize the CAM for en50221
 *****************************************************************************/
void en50221_Init( int adapter )
{
    char psz_tmp[128];
    ca_caps_t caps;

    memset( &caps, 0, sizeof( ca_caps_t ));

    sprintf( psz_tmp, "/dev/dvb/adapter%d/ca0", adapter );
    if( (i_ca_handle = open(psz_tmp, O_RDWR | O_NONBLOCK)) < 0 )
    {
        msg_Warn( NULL, "failed opening CAM device %s (%s)",
                  psz_tmp, strerror(errno) );
        i_ca_handle = 0;
        return;
    }

    if ( ioctl( i_ca_handle, CA_GET_CAP, &caps ) != 0 )
    {
        msg_Err( NULL, "failed getting CAM capabilities (%s)",
                 strerror(errno) );
        close( i_ca_handle );
        i_ca_handle = 0;
        return;
    }

    /* Output CA capabilities */
    msg_Dbg( NULL, "CA interface with %d %s", caps.slot_num,
        caps.slot_num == 1 ? "slot" : "slots" );
    if ( caps.slot_type & CA_CI )
        msg_Dbg( NULL, "  CI high level interface type" );
    if ( caps.slot_type & CA_CI_LINK )
        msg_Dbg( NULL, "  CI link layer level interface type" );
    if ( caps.slot_type & CA_CI_PHYS )
        msg_Dbg( NULL, "  CI physical layer level interface type (not supported) " );
    if ( caps.slot_type & CA_DESCR )
        msg_Dbg( NULL, "  built-in descrambler detected" );
    if ( caps.slot_type & CA_SC )
        msg_Dbg( NULL, "  simple smart card interface" );

    msg_Dbg( NULL, "  %d available %s", caps.descr_num,
        caps.descr_num == 1 ? "descrambler (key)" : "descramblers (keys)" );
    if ( caps.descr_type & CA_ECD )
        msg_Dbg( NULL, "  ECD scrambling system supported" );
    if ( caps.descr_type & CA_NDS )
        msg_Dbg( NULL, "  NDS scrambling system supported" );
    if ( caps.descr_type & CA_DSS )
        msg_Dbg( NULL, "  DSS scrambling system supported" );

    if ( caps.slot_num == 0 )
    {
        msg_Err( NULL, "CAM module with no slots" );
        close( i_ca_handle );
        i_ca_handle = 0;
        return;
    }

    if( caps.slot_type & CA_CI_LINK )
        i_ca_type = CA_CI_LINK;
    else if( caps.slot_type & CA_CI )
        i_ca_type = CA_CI;
    else
    {
        msg_Err( NULL, "Incompatible CAM interface" );
        close( i_ca_handle );
        i_ca_handle = 0;
        return;
    }

    i_nb_slots = caps.slot_num;
    memset( p_sessions, 0, sizeof(en50221_session_t) * MAX_SESSIONS );

    en50221_Reset();
}

/*****************************************************************************
 * en50221_Reset : Reset the CAM for en50221
 *****************************************************************************/
void en50221_Reset( void )
{
    memset( pb_active_slot, 0, sizeof(bool) * MAX_CI_SLOTS );
    memset( pb_tc_has_data, 0, sizeof(bool) * MAX_CI_SLOTS );
    memset( pb_slot_mmi_expected, 0, sizeof(bool) * MAX_CI_SLOTS );
    memset( pb_slot_mmi_undisplayed, 0, sizeof(bool) * MAX_CI_SLOTS );

    if( i_ca_type & CA_CI_LINK )
    {
        int i_slot;
        for ( i_slot = 0; i_slot < i_nb_slots; i_slot++ )
            ResetSlot( i_slot );
    }
    else
    {
        struct ca_slot_info info;
        system_ids_t *p_ids;
        ca_msg_t ca_msg;
        info.num = 0;

        /* We don't reset the CAM in that case because it's done by the
         * ASIC. */
        if ( ioctl( i_ca_handle, CA_GET_SLOT_INFO, &info ) < 0 )
        {
            msg_Err( NULL, "en50221_Init: couldn't get slot info" );
            close( i_ca_handle );
            i_ca_handle = 0;
            return;
        }
        if( info.flags == 0 )
        {
            msg_Err( NULL, "en50221_Init: no CAM inserted" );
            close( i_ca_handle );
            i_ca_handle = 0;
            return;
        }

        /* Allocate a dummy sessions */
        p_sessions[0].i_resource_id = RI_CONDITIONAL_ACCESS_SUPPORT;
        p_sessions[0].pf_close = ConditionalAccessClose;
        if ( p_sessions[0].p_sys == NULL )
            p_sessions[0].p_sys = malloc(sizeof(system_ids_t));
        memset( p_sessions[0].p_sys, 0, sizeof(system_ids_t) );
        p_ids = (system_ids_t *)p_sessions[0].p_sys;
        p_ids->b_high_level = 1;

        /* Get application info to find out which cam we are using and make
           sure everything is ready to play */
        ca_msg.length=3;
        ca_msg.msg[0] = ( AOT_APPLICATION_INFO & 0xFF0000 ) >> 16;
        ca_msg.msg[1] = ( AOT_APPLICATION_INFO & 0x00FF00 ) >> 8;
        ca_msg.msg[2] = ( AOT_APPLICATION_INFO & 0x0000FF ) >> 0;
        memset( &ca_msg.msg[3], 0, 253 );
        APDUSend( NULL, 1, AOT_APPLICATION_INFO_ENQ, NULL, 0 );
        if ( ioctl( i_ca_handle, CA_GET_MSG, &ca_msg ) < 0 )
        {
            msg_Err( NULL, "en50221_Init: failed getting message" );
            close( i_ca_handle );
            i_ca_handle = 0;
            return;
        }

#if HLCI_WAIT_CAM_READY
        while( ca_msg.msg[8] == 0xff && ca_msg.msg[9] == 0xff )
        {
            msleep(1);
            msg_Dbg( NULL, "CAM: please wait" );
            APDUSend( NULL, 1, AOT_APPLICATION_INFO_ENQ, NULL, 0 );
            ca_msg.length=3;
            ca_msg.msg[0] = ( AOT_APPLICATION_INFO & 0xFF0000 ) >> 16;
            ca_msg.msg[1] = ( AOT_APPLICATION_INFO & 0x00FF00 ) >> 8;
            ca_msg.msg[2] = ( AOT_APPLICATION_INFO & 0x0000FF ) >> 0;
            memset( &ca_msg.msg[3], 0, 253 );
            if ( ioctl( i_ca_handle, CA_GET_MSG, &ca_msg ) < 0 )
            {
                msg_Err( NULL, "en50221_Init: failed getting message" );
                close( i_ca_handle );
                i_ca_handle = 0;
                return;
            }
            msg_Dbg( NULL, "en50221_Init: Got length: %d, tag: 0x%x", ca_msg.length, APDUGetTag( ca_msg.msg, ca_msg.length ) );
        }
#else
        if( ca_msg.msg[8] == 0xff && ca_msg.msg[9] == 0xff )
        {
            msg_Err( NULL, "CAM returns garbage as application info!" );
            close( i_ca_handle );
            i_ca_handle = 0;
            return;
        }
#endif
        msg_Dbg( NULL, "found CAM %s using id 0x%x", &ca_msg.msg[12],
                 (ca_msg.msg[8]<<8)|ca_msg.msg[9] );
    }
}

/*****************************************************************************
 * en50221_Poll : Poll the CAM for TPDUs
 *****************************************************************************/
void en50221_Poll( void )
{
    int i_slot;
    int i_session_id;

    for ( i_slot = 0; i_slot < i_nb_slots; i_slot++ )
    {
        uint8_t i_tag;
        ca_slot_info_t sinfo;

        sinfo.num = i_slot;
        if ( ioctl( i_ca_handle, CA_GET_SLOT_INFO, &sinfo ) != 0 )
        {
            msg_Err( NULL, "en50221_Poll: couldn't get info on slot %d",
                     i_slot );
            continue;
        }

        if ( !(sinfo.flags & CA_CI_MODULE_READY) )
        {
            if ( pb_active_slot[i_slot] )
            {
                msg_Dbg( NULL, "en50221_Poll: slot %d has been removed",
                         i_slot );
                pb_active_slot[i_slot] = false;
                pb_slot_mmi_expected[i_slot] = false;
                pb_slot_mmi_undisplayed[i_slot] = false;

                /* Close all sessions for this slot. */
                for ( i_session_id = 1; i_session_id <= MAX_SESSIONS;
                      i_session_id++ )
                {
                    if ( p_sessions[i_session_id - 1].i_resource_id
                          && p_sessions[i_session_id - 1].i_slot
                               == i_slot )
                    {
                        if ( p_sessions[i_session_id - 1].pf_close
                              != NULL )
                        {
                            p_sessions[i_session_id - 1].pf_close(
                                                NULL, i_session_id );
                        }
                        p_sessions[i_session_id - 1].i_resource_id = 0;
                    }
                }
            }

            continue;
        }
        else if ( !pb_active_slot[i_slot] )
        {
            InitSlot( NULL, i_slot );

            if ( !pb_active_slot[i_slot] )
            {
                msg_Dbg( NULL, "en50221_Poll: resetting slot %d", i_slot );
                ResetSlot( i_slot );
                continue;
            }

            msg_Dbg( NULL, "en50221_Poll: slot %d is active",
                     i_slot );
        }

        if ( !pb_tc_has_data[i_slot] )
        {
            if ( TPDUSend( NULL, i_slot, T_DATA_LAST, NULL, 0 ) != 0 )
            {
                msg_Err( NULL,
                         "en50221_Poll: couldn't send TPDU on slot %d, resetting",
                         i_slot );
                ResetSlot( i_slot );
                continue;
            }
            if ( TPDURecv( NULL, i_slot, &i_tag, NULL, NULL ) != 0 )
            {
                msg_Err( NULL,
                         "en50221_Poll: couldn't recv TPDU on slot %d, resetting",
                         i_slot );
                ResetSlot( i_slot );
                continue;
            }
        }

        while ( pb_tc_has_data[i_slot] )
        {
            uint8_t p_tpdu[MAX_TPDU_SIZE];
            int i_size, i_session_size;
            uint8_t *p_session;

            if ( TPDUSend( NULL, i_slot, T_RCV, NULL, 0 ) != 0 )
            {
                msg_Err( NULL,
                         "en50221_Poll: couldn't send TPDU on slot %d, resetting",
                         i_slot );
                ResetSlot( i_slot );
                continue;
            }
            if ( TPDURecv( NULL, i_slot, &i_tag, p_tpdu, &i_size ) != 0 )
            {
                msg_Err( NULL,
                         "en50221_Poll: couldn't recv TPDU on slot %d, resetting",
                         i_slot );
                ResetSlot( i_slot );
                continue;
            }

            p_session = GetLength( &p_tpdu[3], &i_session_size );
            if ( i_session_size <= 1 )
                continue;

            p_session++;
            i_session_size--;

            if ( i_tag != T_DATA_LAST )
            {
                /* I sometimes see a CAM responding T_SB to our T_RCV.
                 * It said it had data to send, but does not send it after
                 * our T_RCV. There is probably something wrong here. I
                 * experienced that this case happens on start-up, and the
                 * CAM doesn't open any session at all, so it is quite
                 * useless. Reset it. */
                msg_Err( NULL,
                         "en50221_Poll: invalid TPDU 0x%x, resetting", i_tag );
                ResetSlot( i_slot );
                break;
            }

            SPDUHandle( NULL, i_slot, p_session, i_session_size );
        }
    }

    for ( i_session_id = 1; i_session_id <= MAX_SESSIONS; i_session_id++ )
    {
        if ( p_sessions[i_session_id - 1].i_resource_id
              && p_sessions[i_session_id - 1].pf_manage )
        {
            p_sessions[i_session_id - 1].pf_manage( NULL, i_session_id );
        }
    }
}

/*****************************************************************************
 * en50221_AddPMT :
 *****************************************************************************/
void en50221_AddPMT( dvbpsi_pmt_t *p_pmt )
{
    int i_session_id;

    for ( i_session_id = 1; i_session_id <= MAX_SESSIONS; i_session_id++ )
        if ( p_sessions[i_session_id - 1].i_resource_id
                == RI_CONDITIONAL_ACCESS_SUPPORT )
            CAPMTAdd( NULL, i_session_id, p_pmt );
}

/*****************************************************************************
 * en50221_UpdatePMT :
 *****************************************************************************/
void en50221_UpdatePMT( dvbpsi_pmt_t *p_pmt )
{
    int i_session_id;

    for ( i_session_id = 1; i_session_id <= MAX_SESSIONS; i_session_id++ )
        if ( p_sessions[i_session_id - 1].i_resource_id
                == RI_CONDITIONAL_ACCESS_SUPPORT )
            CAPMTUpdate( NULL, i_session_id, p_pmt );
}

/*****************************************************************************
 * en50221_DeletePMT :
 *****************************************************************************/
void en50221_DeletePMT( dvbpsi_pmt_t *p_pmt )
{
    int i_session_id;

    for ( i_session_id = 1; i_session_id <= MAX_SESSIONS; i_session_id++ )
        if ( p_sessions[i_session_id - 1].i_resource_id
                == RI_CONDITIONAL_ACCESS_SUPPORT )
            CAPMTDelete( NULL, i_session_id, p_pmt );
}

/*****************************************************************************
 * en50221_StatusMMI :
 *****************************************************************************/
uint8_t en50221_StatusMMI( uint8_t *p_answer, ssize_t *pi_size )
{
    struct ret_mmi_status *p_ret = (struct ret_mmi_status *)p_answer;

    if ( ioctl( i_ca_handle, CA_GET_CAP, &p_ret->caps ) != 0 )
    {
        msg_Err( NULL, "ioctl CA_GET_CAP failed (%s)", strerror(errno) );
        return RET_ERR;
    }

    *pi_size = sizeof(struct ret_mmi_status);
    return RET_MMI_STATUS;
}

/*****************************************************************************
 * en50221_StatusMMISlot :
 *****************************************************************************/
uint8_t en50221_StatusMMISlot( uint8_t *p_buffer, ssize_t i_size,
                               uint8_t *p_answer, ssize_t *pi_size )
{
    int i_slot;
    struct ret_mmi_slot_status *p_ret = (struct ret_mmi_slot_status *)p_answer;

    if ( i_size != 1 ) return RET_HUH;
    i_slot = *p_buffer;

    p_ret->sinfo.num = i_slot;
    if ( ioctl( i_ca_handle, CA_GET_SLOT_INFO, &p_ret->sinfo ) != 0 )
    {
        msg_Err( NULL, "ioctl CA_GET_SLOT_INFO failed (%s)", strerror(errno) );
        return RET_ERR;
    }

    *pi_size = sizeof(struct ret_mmi_slot_status);
    return RET_MMI_SLOT_STATUS;
}

/*****************************************************************************
 * en50221_OpenMMI :
 *****************************************************************************/
uint8_t en50221_OpenMMI( uint8_t *p_buffer, ssize_t i_size )
{
    int i_slot;

    if ( i_size != 1 ) return RET_HUH;
    i_slot = *p_buffer;

    if( i_ca_type & CA_CI_LINK )
    {
        int i_session_id;
        for ( i_session_id = 1; i_session_id <= MAX_SESSIONS; i_session_id++ )
        {
            if ( p_sessions[i_session_id - 1].i_resource_id == RI_MMI
                  && p_sessions[i_session_id - 1].i_slot == i_slot )
            {
                msg_Dbg( NULL,
                         "MMI menu is already opened on slot %d (session=%d)",
                         i_slot, i_session_id );
                return RET_OK;
            }
        }

        for ( i_session_id = 1; i_session_id <= MAX_SESSIONS; i_session_id++ )
        {
            if ( p_sessions[i_session_id - 1].i_resource_id
                    == RI_APPLICATION_INFORMATION
                  && p_sessions[i_session_id - 1].i_slot == i_slot )
            {
                ApplicationInformationEnterMenu( NULL, i_session_id );
                return RET_OK;
            }
        }

        msg_Err( NULL, "no application information on slot %d", i_slot );
        return RET_ERR;
    }
    else
    {
        msg_Err( NULL, "MMI menu not supported" );
        return RET_ERR;
    }
}

/*****************************************************************************
 * en50221_CloseMMI :
 *****************************************************************************/
uint8_t en50221_CloseMMI( uint8_t *p_buffer, ssize_t i_size )
{
    int i_slot;

    if ( i_size != 1 ) return RET_HUH;
    i_slot = *p_buffer;

    if( i_ca_type & CA_CI_LINK )
    {
        int i_session_id;
        for ( i_session_id = 1; i_session_id <= MAX_SESSIONS; i_session_id++ )
        {
            if ( p_sessions[i_session_id - 1].i_resource_id == RI_MMI
                  && p_sessions[i_session_id - 1].i_slot == i_slot )
            {
                MMISendClose( NULL, i_session_id );
                return RET_OK;
            }
        }

        msg_Warn( NULL, "closing a non-existing MMI session on slot %d",
                  i_slot );
        return RET_ERR;
    }
    else
    {
        msg_Err( NULL, "MMI menu not supported" );
        return RET_ERR;
    }
}

/*****************************************************************************
 * en50221_GetMMIObject :
 *****************************************************************************/
uint8_t en50221_GetMMIObject( uint8_t *p_buffer, ssize_t i_size,
                              uint8_t *p_answer, ssize_t *pi_size )
{
    int i_session_id, i_slot;
    struct ret_mmi_recv *p_ret = (struct ret_mmi_recv *)p_answer;

    if ( i_size != 1 ) return RET_HUH;
    i_slot = *p_buffer;

    if ( pb_slot_mmi_expected[i_slot] == true )
        return RET_MMI_WAIT; /* data not yet available */

    p_ret->object.i_object_type = EN50221_MMI_NONE;
    *pi_size = sizeof(struct ret_mmi_recv);

    for ( i_session_id = 1; i_session_id <= MAX_SESSIONS; i_session_id++ )
    {
        if ( p_sessions[i_session_id - 1].i_resource_id == RI_MMI
              && p_sessions[i_session_id - 1].i_slot == i_slot )
        {
            mmi_t *p_mmi =
                (mmi_t *)p_sessions[i_session_id - 1].p_sys;
            if ( p_mmi == NULL )
            {
                *pi_size = 0;
                return RET_ERR; /* should not happen */
            }

            *pi_size = COMM_BUFFER_SIZE - COMM_HEADER_SIZE -
                        ((void *)&p_ret->object - (void *)p_ret);
            if ( en50221_SerializeMMIObject( (uint8_t *)&p_ret->object,
                               pi_size, &p_mmi->last_object ) == -1 )
            {
                *pi_size = 0;
                msg_Err( NULL, "MMI structure too big" );
                return RET_ERR;
            }
            *pi_size += ((void *)&p_ret->object - (void *)p_ret);
            break;
        }
    }

    return RET_MMI_RECV;
}


/*****************************************************************************
 * en50221_SendMMIObject :
 *****************************************************************************/
uint8_t en50221_SendMMIObject( uint8_t *p_buffer, ssize_t i_size )
{
    int i_session_id, i_slot;
    struct cmd_mmi_send *p_cmd = (struct cmd_mmi_send *)p_buffer;

    if ( en50221_UnserializeMMIObject( &p_cmd->object, i_size -
                         ((void *)&p_cmd->object - (void *)p_cmd) ) == -1 )
         return RET_ERR;

    i_slot = p_cmd->i_slot;

    for ( i_session_id = 1; i_session_id <= MAX_SESSIONS; i_session_id++ )
    {
        if ( p_sessions[i_session_id - 1].i_resource_id == RI_MMI
              && p_sessions[i_session_id - 1].i_slot == i_slot )
        {
            MMISendObject( NULL, i_session_id, &p_cmd->object );
            return RET_OK;
        }
    }

    msg_Err( NULL, "SendMMIObject when no MMI session is opened !" );
    return RET_ERR;
}
