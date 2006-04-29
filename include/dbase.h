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
 
dbase.h
 
Opens/Closes and setups the sqlite database for use by the rest of the application.
 
*/

#ifndef _DBASE_H
#define _DBASE_H
#include <sqlite3.h>

#define SERVICES_TABLE          "Services"
#define SERVICE_MPLEXFREQ       "mplexfreq"
#define SERVICE_ID              "id"
#define SERVICE_NAME            "name"
#define SERVICE_PMTPID          "pmtpid"
#define SERVICE_PMTVERSION      "pmtversion"

#define MULTIPLEXES_TABLE       "Multiplexes"
#define MULTIPLEX_FREQ          "freq"
#define MULTIPLEX_ID            "id"
#define MULTIPLEX_TYPE          "type"
#define MULTIPLEX_PATVERSION    "patversion"

#define OFDMPARAMS_TABLE        "OFDMParameters"
#define OFDMPARAM_FREQ          "freq"
#define OFDMPARAM_INVERSION     "inversion"
#define OFDMPARAM_BW            "bw"
#define OFDMPARAM_FEC_HP        "fec_hp"
#define OFDMPARAM_FEC_LP        "fec_lp"
#define OFDMPARAM_QAM           "qam"
#define OFDMPARAM_TRANSMISSIONM "transmissionm"
#define OFDMPARAM_GUARDLIST     "guardlist"
#define OFDMPARAM_HIERARCHINFO  "hierarchinfo"

#define PIDS_TABLE              "PIDs"
#define PID_MPLEXFREQ           "mplexfreq"
#define PID_SERVICEID           "serviceid"
#define PID_PID                 "pid"
#define PID_TYPE                "type"
#define PID_SUBTYPE             "subtype"
#define PID_PMTVERSION          "pmtversion"

#define VERSION_TABLE           "Version"
#define VERSION_VERSION         "version"


#define STATEMENT_INIT int rc; sqlite3_stmt *stmt=NULL
#define STATEMENT_PREPARE(_statement) rc = sqlite3_prepare( DBaseInstance,  _statement, -1, &stmt, NULL)
#define STATEMENT_PREPAREVA(_statement, _args...) \
    do{\
        char *sqlstring;\
        sqlstring = sqlite3_mprintf(_statement, _args);\
        if (sqlstring)\
        {\
            STATEMENT_PREPARE(sqlstring);\
            sqlite3_free(sqlstring);\
        }\
        else\
        {\
            rc = SQLITE_NOMEM;\
        }\
    }while(0)

#define STATEMENT_STEP()              rc = sqlite3_step(stmt)
#define STATEMENT_COLUMN_INT(_index)  sqlite3_column_int(stmt, _index)
#define STATEMENT_COLUMN_TEXT(_index) (char*)sqlite3_column_text( stmt, _index)
#define STATEMENT_FINALIZE()          rc = sqlite3_finalize(stmt)

#define PRINTLOG_SQLITE3ERROR() \
    do{\
        printlog(LOG_ERROR, "%s(%d): Failed with error code 0x%x = %s\n",__FUNCTION__,__LINE__, rc, sqlite3_errmsg(DBaseInstance));\
    }while(0)

#define RETURN_ON_ERROR(_result) \
    do{\
        if ((rc != SQLITE_OK) && (rc != SQLITE_ROW) && (rc != SQLITE_DONE))\
        {\
            PRINTLOG_SQLITE3ERROR();\
            if (stmt)\
            {\
                STATEMENT_FINALIZE();\
            }\
            return _result;\
        }\
    }while(0)

#define RETURN_RC_ON_ERROR RETURN_ON_ERROR(rc)

extern sqlite3 *DBaseInstance;

int DBaseInit(int adapter);
void DBaseDeInit();

#endif
