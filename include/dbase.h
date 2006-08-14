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

/** @defgroup DatabaseConstants Database Constants
 * @{
 */

/**
 * Constant for Services Table name
 */
#define SERVICES_TABLE          "Services"
/**
 * Constant for Service Table Column name
 */
#define SERVICE_MPLEXFREQ       "mplexfreq"
/**
 * Constant for Service Table Column name
 */
#define SERVICE_ID              "id"
/**
 * Constant for Service Table Column name
 */
#define SERVICE_NAME            "name"
/**
 * Constant for Service Table Column name
 */
#define SERVICE_PMTPID          "pmtpid"
/**
 * Constant for Service Table Column name
 */
#define SERVICE_PMTVERSION      "pmtversion"

/**
 * Constant for the Multiplexes table name.
 */
#define MULTIPLEXES_TABLE       "Multiplexes"
/**
 * Constant for Multiplex column name
 */
#define MULTIPLEX_FREQ          "freq"
/**
 * Constant for Multiplex column name
 */
#define MULTIPLEX_ID            "id"
/**
 * Constant for Multiplex column name
 */
#define MULTIPLEX_NETID         "netid"
/**
 * Constant for Multiplex column name
 */
#define MULTIPLEX_TYPE          "type"
/**
 * Constant for Multiplex column name
 */
#define MULTIPLEX_PATVERSION    "patversion"

/**
 * Constant for OFDMParameters table name,
 */
#define OFDMPARAMS_TABLE        "OFDMParameters"
/**
 * Constant for OFDMParameters column name.
 */
#define OFDMPARAM_FREQ          "freq"
/**
 * Constant for OFDMParameters column name.
 */
#define OFDMPARAM_INVERSION     "inversion"
/**
 * Constant for OFDMParameters column name.
 */
#define OFDMPARAM_BW            "bw"
/**
 * Constant for OFDMParameters column name.
 */
#define OFDMPARAM_FEC_HP        "fec_hp"
/**
 * Constant for OFDMParameters column name.
 */
#define OFDMPARAM_FEC_LP        "fec_lp"
/**
 * Constant for OFDMParameters column name.
 */
#define OFDMPARAM_QAM           "qam"
/**
 * Constant for OFDMParameters column name.
 */
#define OFDMPARAM_TRANSMISSIONM "transmissionm"
/**
 * Constant for OFDMParameters column name.
 */
#define OFDMPARAM_GUARDLIST     "guardlist"
/**
 * Constant for OFDMParameters column name.
 */
#define OFDMPARAM_HIERARCHINFO  "hierarchinfo"

/**
 * Constant for QPSKParameters table name.
 */
#define QPSKPARAMS_TABLE		"QPSKParameters"
/**
 * Constant for QPSKParameters column name.
 */
#define QPSKPARAM_FREQ          "freq"
/**
 * Constant for QPSKParameters column name.
 */
#define QPSKPARAM_INVERSION     "inversion"
/**
 * Constant for QPSKParameters column name.
 */
#define QPSKPARAM_SYMBOL_RATE    "symbol_rate"
/**
 * Constant for QPSKParameters column name.
 */
#define QPSKPARAM_FEC_INNER      "fec_inner"

/**
 * Constant for QAMParameters table name.
 */
#define QAMPARAMS_TABLE			"QAMParameters"
/**
 * Constant for QAMParameters column name.
 */
#define QAMPARAM_FREQ           "freq"
/**
 * Constant for QAMParameters column name.
 */
#define QAMPARAM_INVERSION      "inversion"
/**
 * Constant for QAMParameters column name.
 */
#define QAMPARAM_SYMBOL_RATE    "symbol_rate"
/**
 * Constant for QAMParameters column name.
 */
#define QAMPARAM_FEC_INNER      "fec_inner"
/**
 * Constant for QAMParameters column name.
 */
#define QAMPARAM_MODULATION     "modulation"

/**
 * Constant for the PIDs table name,
 */
#define PIDS_TABLE              "PIDs"

/**
 * Constant for PID column name.
 */
#define PID_MPLEXFREQ           "mplexfreq"
/**
 * Constant for PID column name.
 */
#define PID_SERVICEID           "serviceid"
/**
 * Constant for PID column name.
 */
#define PID_PID                 "pid"
/**
 * Constant for PID column name.
 */
#define PID_TYPE                "type"
/**
 * Constant for PID column name.
 */
#define PID_SUBTYPE             "subtype"
/**
 * Constant for PID column name.
 */
#define PID_PMTVERSION          "pmtversion"

/**
 * Constant for Version table name.
 */
#define VERSION_TABLE           "Version"
/**
 * Constant for Version column name.
 */
#define VERSION_VERSION         "version"

/** @} */

/**
 * @defgroup Database Database Macros and Functions
 * @{
 */

/**
 * Macro to define the varaiable required for executing an sql statement,
 * Only 1 call to STATEMENT_INIT per function is supported!
 */
#define STATEMENT_INIT int rc; sqlite3_stmt *stmt=NULL

/**
 * Macro to prepare an sql statement.
 * @param _statement The sql statement to prepare.
 */
#define STATEMENT_PREPARE(_statement) rc = sqlite3_prepare( DBaseInstance,  _statement, -1, &stmt, NULL)

/**
 * Macro to prepare an sql statement with arguments.
 * @param _statement The sql statement to prepare.
 * @param _args The arguments to be inserted into the statement.
 */
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

/**
 * Macro to perform a 'step' on an sql statement.
 */
#define STATEMENT_STEP()                rc = sqlite3_step(stmt)

/**
 * Macro to retrieve an int value from the result of a 'step'.
 * @param _index Column index of the result to retrieve.
 */
#define STATEMENT_COLUMN_INT(_index)    sqlite3_column_int(stmt, _index)
/**
 * Macro to retrieve a double value from the result of a 'step'.
 * @param _index Column index of the result to retrieve.
 */
#define STATEMENT_COLUMN_DOUBLE(_index) sqlite3_column_double(stmt, _index)

/**
 * Macro to retrieve a string from the result of a 'step'.
 * The string should be free'd using the sqlite_free function when it is no
 * longer required.
 * @param _index Column index of the result to retrieve.
 */
#define STATEMENT_COLUMN_TEXT(_index)   (char*)sqlite3_column_text( stmt, _index)

/**
 * Macro to finalise an sql statement.
 */
#define STATEMENT_FINALIZE()            rc = sqlite3_finalize(stmt)

/**
 * Macro to log the last SQLite error.
 * This macro logs at the LOG_ERROR level.
 */
#define PRINTLOG_SQLITE3ERROR() \
    do{\
        printlog(LOG_ERROR, "%s(%d): Failed with error code 0x%x = %s\n",__FUNCTION__,__LINE__, rc, sqlite3_errmsg(DBaseInstance));\
    }while(0)

/**
 * Macro to tidy up an sql statement and return the specified value if an error occured.
 *
 * This macro logs the sqlite error, finalises the sql statement and returns if
 * the error was not SQLITE_OK, SQLITE_ROW or SQLITE_DONE.
 * @param _result The value to return from the current function.
 */
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

/**
 * Macro to tidy up an sql statemente and return the last result from an sqlite call.
 */
#define RETURN_RC_ON_ERROR RETURN_ON_ERROR(rc)

/**
 * @internal
 * Global variable containing the handle to the sqlite database being used.
 */
extern sqlite3 *DBaseInstance;

/**
 * @internal
 * Initialise the database for the given adapter.
 * This function will create the database if one doesn't exist,
 * and upgrade the database if it is a different version to the one being used
 * by the application.
 * @param adapter The DVB adapter number to open the database of.
 * @return 0 on success, otherwise an SQLite error code.
 */
int DBaseInit(int adapter);

/**
 * @internal
 * De-initialise the database.
 */
void DBaseDeInit();

/** @} */
#endif
