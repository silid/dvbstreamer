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
#define SERVICE_MULTIPLEXUID    "mplexuid"
/**
 * Constant for Service Table Column name
 */
#define SERVICE_ID              "id"
/**
 * Constant for Service Table Column name
 */
#define SERVICE_SOURCE          "source"
/**
 * Constant for Service Table Column name
 */
#define SERVICE_CA              "ca"
/**
 * Constant for Service Table Column name
 */
#define SERVICE_NAME            "name"
/**
 * Constant for Service Table Column name
 */
#define SERVICE_TYPE            "type"
/**
 * Constant for Service Table Column name
 */
#define SERVICE_PMTPID          "pmtpid"
/**
 * Constant for Service Table Column name
 */
#define SERVICE_PMTVERSION      "pmtversion"
/**
 * Constant for Service Table Column name
 */
#define SERVICE_PCRPID          "pcrpid"
/**
 * Constant for Service Table Column name
 */
#define SERVICE_PROVIDER        "provider"
/**
 * Constant for Service Table Column name
 */
#define SERVICE_DEFAUTHORITY    "defauthority"

/**
 * Constant for the Multiplexes table name.
 */
#define MULTIPLEXES_TABLE       "Multiplexes"
/**
 * Constant for Multiplex column name
 */
#define MULTIPLEX_UID           "uid"
/**
 * Constant for Multiplex column name
 */
#define MULTIPLEX_FREQ          "freq"
/**
 * Constant for Multiplex column name
 */
#define MULTIPLEX_TSID          "tsid"
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
#define OFDMPARAM_MULTIPLEXUID  "mplexuid"
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
#define QPSKPARAMS_TABLE  "QPSKParameters"
/**
 * Constant for QPSKParameters column name.
 */
#define QPSKPARAM_MULTIPLEXUID  "mplexuid"
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
 * Constant for QPSKParameters column name.
 */
#define QPSKPARAM_POLARISATION   "polarisation"
/**
 * Constant for QPSKParameters column name.
 */
#define QPSKPARAM_SATNUMBER      "satnumber"

/**
 * Constant for QAMParameters table name.
 */
#define QAMPARAMS_TABLE   "QAMParameters"
/**
 * Constant for QAMParameters column name.
 */
#define QAMPARAM_MULTIPLEXUID  "mplexuid"
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
 * Constant for VSBParameters table name.
 */
#define VSBPARAMS_TABLE   "VSBParameters"
/**
 * Constant for VSBParameters column name.
 */
#define VSBPARAM_MULTIPLEXUID  "mplexuid"
/**
 * Constant for VSBParameters column name.
 */
#define VSBPARAM_FREQ           "freq"
/**
 * Constant for VSBParameters column name.
 */
#define VSBPARAM_MODULATION     "modulation"

/**
 * Constant for the PIDs table name,
 */
#define PIDS_TABLE              "PIDs"
/**
 * Constant for PID column name.
 */
#define PID_MULTIPLEXUID        "mplexuid"
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
 * Constant for PID column name.
 */
#define PID_DESCRIPTORS         "descriptors"

/**
 * Constant for Metadata table name.
 */
#define METADATA_TABLE           "Metadata"
/**
 * Constant for Metadata column name.
 */
#define METADATA_NAME            "name"
/**
 * Constant for Metadata column name.
 */
#define METADATA_VALUE           "value"

/**
 * Constant for the EPG Events table.
 */
#define EPGEVENTS_TABLE          "EPGEvents"
/**
 * Constant for EPG Event column name.
 */
#define EPGEVENT_NETID           "netid"
/**
 * Constant for EPG Event column name.
 */
#define EPGEVENT_TSID            "tsid"
/**
 * Constant for EPG Event column name.
 */
#define EPGEVENT_SERVICEID       "serviceid"
/**
 * Constant for EPG Event column name.
 */
#define EPGEVENT_EVENTID         "eventid"
/**
 * Constant for EPG Event column name.
 */
#define EPGEVENT_STARTTIME       "starttime"
/**
 * Constant for EPG Event column name.
 */
#define EPGEVENT_ENDTIME         "endtime"
/**
 * Constant for EPG Event column name.
 */
#define EPGEVENT_CA              "ca"

/**
 * Constant for the EPG Ratings table.
 */
#define EPGRATINGS_TABLE         "EPGRatings"
/**
 * Constant for EPG Rating column name.
 */
#define EPGRATING_ID             "id"
/**
 * Constant for EPG Rating column name.
 */
#define EPGRATING_EVENTUID         "eventuid"
/**
 * Constant for EPG Rating column name.
 */
#define EPGRATING_STANDARD        "standard"
/**
 * Constant for EPG Rating column name.
 */
#define EPGRATING_RATING          "rating"

/**
 * Constant for the EPG Details table.
 */
#define EPGDETAILS_TABLE         "EPGDetails"
/**
 * Constant for EPG Detail column name.
 */
#define EPGDETAIL_ID             "id"
/**
 * Constant for EPG Detail column name.
 */
#define EPGDETAIL_EVENTUID         "eventuid"
/**
 * Constant for EPG Detail column name.
 */
#define EPGDETAIL_LANGUAGE        "lang"
/**
 * Constant for EPG Detail column name.
 */
#define EPGDETAIL_NAME            "name"
/**
 * Constant for EPG Detail column name.
 */
#define EPGDETAIL_VALUE           "value"

/**
 * Constant for Metadata property for LNB settings.
 */
#define METADATA_NAME_LNB_LOW_FREQ     "lnb.lowfreq"
/**
 * Constant for Metadata property for LNB settings.
 */
#define METADATA_NAME_LNB_HIGH_FREQ    "lnb.highfreq"
/**
 * Constant for Metadata property for LNB settings.
 */
#define METADATA_NAME_LNB_SWITCH_FREQ  "lnb.switchfreq"

/**
 * Constant for Metadata property to scan all multiplexes on startup.
 */
#define METADATA_NAME_SCAN_ALL "scan.all"
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

#ifndef DBASE_CONNECTION_GET
#define DBASE_CONNECTION_GET DBaseConnectionGet
#endif

/**
 * Macro to prepare an sql statement.
 * @param _statement The sql statement to prepare.
 */
#define STATEMENT_PREPARE(_statement) rc = sqlite3_prepare( DBASE_CONNECTION_GET(),  _statement, -1, &stmt, NULL)

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
        LogModule(LOG_DEBUG, "dbase", "%s(%d): Failed with error code 0x%x=%s\n",\
            __FUNCTION__,__LINE__, rc, sqlite3_errmsg(DBASE_CONNECTION_GET()));\
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

/**
 * Get the sqlite3 connection object for this thread.
 * @return An sqlite3 connection object or NULL if the database could not be opened.
 */
sqlite3* DBaseConnectionGet(void);
    
/**
 * Start a transaction on the database.
 * Can be used to increase the speed when reading from multiple tables.
 * @return 0 on success, otherwise an SQLite error code. 
 */
int DBaseTransactionBegin(void);

/**
 * Commit a transaction on the database.
 * @return 0 on success, otherwise an SQLite error code.
 */
int DBaseTransactionCommit(void);

/**
 * Retrieve the specified metadata property.
 * @param name The name of the property.
 * @param value The location to store the value in. This should must be free'd 
 *              once finished with.
 * @return 0 on success, otherwise an SQLite error code.
 */
int DBaseMetadataGet(char *name, char **value);

/**
 * Set the specified metadata property to the string specified.
 * @param name The name of the property.
 * @param value The value to set it to.
 * @return 0 on success, otherwise an SQLite error code.
 */
int DBaseMetadataSet(char *name, char *value);

/**
 * Retrieve the specified metadata property.
 * @param name The name of the property.
 * @param value The location to store the value in.
 * @return 0 on success, otherwise an SQLite error code.
 */
int DBaseMetadataGetInt(char *name, int *value);
/**
 * Set the specified metadata property to the int specified.
 * @param name The name of the property.
 * @param value The value to set it to.
 * @return 0 on success, otherwise an SQLite error code.
 */
int DBaseMetadataSetInt(char *name, int value);

/**
 * Retrieve the specified metadata property.
 * @param name The name of the property.
 * @param value The location to store the value in.
 * @return 0 on success, otherwise an SQLite error code.
 */
int DBaseMetadataGetDouble(char *name, double *value);

/**
 * Set the specified metadata property to the double specified.
 * @param name The name of the property.
 * @param value The value to set it to.
 * @return 0 on success, otherwise an SQLite error code.
 */
int DBaseMetadataSetDouble(char *name, double value);

/**
 * Delete the specified metadata property.
 * @param name The name of the property.
 * @return 0 on success, otherwise an SQLite error code.
 */
int DBaseMetadataDelete(char *name);

/** @} */
#endif
