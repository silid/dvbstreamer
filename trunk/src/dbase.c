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

dbase.c

Opens/Closes and setups the sqlite database for use by the rest of the application.

*/

#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <sys/types.h>

#include "dbase.h"
#include "types.h"
#include "main.h"
#include "logging.h"

/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/

/* This is the version of the database not the application!*/
#define METADATA_DBASE_VERSION "dbase_version"
#define DBASE_VERSION 0.5

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static int DBaseCreateTables(double version);
static int DBaseCheckVersion();

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
sqlite3 *DBaseInstance;

static char DBASE[] = "dbase";

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
int DBaseInit(int adapter)
{
    char file[PATH_MAX];
    int rc;

    sprintf(file, "%s/adapter%d.db", DataDirectory, adapter);
    rc = sqlite3_open(file, &DBaseInstance);
    if (rc)
    {
        LogModule(LOG_ERROR, DBASE, "Can't open database: %s\n", sqlite3_errmsg(DBaseInstance));
        sqlite3_close(DBaseInstance);
    }
    else
    {
        rc = DBaseCheckVersion();
    }
    return rc;
}


void DBaseDeInit()
{
    sqlite3_close(DBaseInstance);
}

/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/
static int DBaseCheckVersion()
{
    int rc;
    double version;
    rc = DBaseMetadataGetDouble(METADATA_DBASE_VERSION, &version);
    if (rc)
    {
        LogModule(LOG_DEBUG, DBASE, "Failed to get version from Metadata table (%d)\n", rc);
        version = 0.0f;
    }
    LogModule(LOG_DEBUG, DBASE, "Current version of database is %f\n", version);
    /* Check version number and upgrade tables for future releases ? */
    if (version < DBASE_VERSION)
    {
        rc = DBaseCreateTables(version);
    }
    return rc;
}

static int DBaseCreateTables(double version)
{
    int rc;

    LogModule(LOG_DEBUG, DBASE, "Creating tables\n");
    sqlite3_exec(DBaseInstance, "BEGIN TRANSACTION;", NULL, NULL, NULL);

    if (version < 0.5)
    {
        sqlite3_exec(DBaseInstance, "DROP TABLE " SERVICES_TABLE ";DROP TABLE " MULTIPLEXES_TABLE ";" 
                                    "DROP TABLE " PIDS_TABLE ";DROP TABLE " OFDMPARAMS_TABLE ";"
                                    "DROP TABLE " QPSKPARAMS_TABLE ";DROP TABLE " QAMPARAMS_TABLE";"
                                    , NULL, NULL, NULL);   

        sqlite3_exec(DBaseInstance, "DROP TABLE Version;", NULL, NULL, NULL);
        
        rc = sqlite3_exec(DBaseInstance, "CREATE TABLE " METADATA_TABLE " ( "
                          METADATA_NAME " PRIMARY KEY,"
                          METADATA_VALUE
                          ");", NULL, NULL, NULL);
        if (rc)
        {
            LogModule(LOG_ERROR, DBASE, "Failed to create Metadata table: %s\n", sqlite3_errmsg(DBaseInstance));
            return rc;
        }

        
        rc = sqlite3_exec(DBaseInstance, "CREATE TABLE " SERVICES_TABLE " ( "
                          SERVICE_MULTIPLEXUID ","
                          SERVICE_ID ","
                          SERVICE_NAME ","
                          SERVICE_PMTPID " DEFAULT -1,"
                          SERVICE_PMTVERSION " DEFAULT -1,"
                          SERVICE_PCRPID " DEFAULT -1,"
                          "PRIMARY KEY ( "SERVICE_MULTIPLEXUID "," SERVICE_ID ")"
                          ");", NULL, NULL, NULL);
        if (rc)
        {
            LogModule(LOG_ERROR, DBASE, "Failed to create Services table: %s\n", sqlite3_errmsg(DBaseInstance));
            return rc;
        }

        rc = sqlite3_exec(DBaseInstance, "CREATE TABLE " MULTIPLEXES_TABLE " ( "
                          MULTIPLEX_UID " INTEGER PRIMARY KEY,"
                          MULTIPLEX_FREQ ","
                          MULTIPLEX_TYPE ","
                          MULTIPLEX_TSID " DEFAULT -1,"
                          MULTIPLEX_NETID" DEFAULT -1,"
                          MULTIPLEX_PATVERSION " DEFAULT -1"
                          ");", NULL, NULL, NULL);
        if (rc)
        {
            LogModule(LOG_ERROR, DBASE, "Failed to create Multiplexes table: %s\n", sqlite3_errmsg(DBaseInstance));
            return rc;
        }

        rc = sqlite3_exec(DBaseInstance, "CREATE TABLE " PIDS_TABLE " ( "
                          PID_MULTIPLEXUID ","
                          PID_SERVICEID ","
                          PID_PID ","
                          PID_TYPE ","
                          PID_SUBTYPE ","
                          PID_PMTVERSION ","
                          PID_DESCRIPTORS " DEFAULT NULL,"
                          "PRIMARY KEY(" PID_MULTIPLEXUID "," PID_SERVICEID "," PID_PID ")"
                          ");", NULL, NULL, NULL);
        if (rc)
        {
            LogModule(LOG_ERROR, DBASE, "Failed to create OFDMParameters table: %s\n", sqlite3_errmsg(DBaseInstance));
            return rc;
        }
        
        /*
        (DVBT) OFDM: <frequency>:<inversion>:<bw>:<fec_hp>:<fec_lp>:<qam>:<transmissionm>:<guardlist>:<hierarchinfo>
        */
        rc = sqlite3_exec(DBaseInstance, "CREATE TABLE " OFDMPARAMS_TABLE " ( "
                          OFDMPARAM_MULTIPLEXUID " PRIMARY KEY,"
                          OFDMPARAM_FREQ ","
                          OFDMPARAM_INVERSION ","
                          OFDMPARAM_BW ","
                          OFDMPARAM_FEC_HP ","
                          OFDMPARAM_FEC_LP ","
                          OFDMPARAM_QAM ","
                          OFDMPARAM_TRANSMISSIONM ","
                          OFDMPARAM_GUARDLIST ","
                          OFDMPARAM_HIERARCHINFO
                          ");", NULL, NULL, NULL);
        if (rc)
        {
            LogModule(LOG_ERROR, DBASE, "Failed to create OFDMParameters table: %s\n", sqlite3_errmsg(DBaseInstance));
            return rc;
        }




        /*
        (DVBS) QPSK: <channel name>:<frequency>:<polarisation>:<sat_no>:<sym_rate>
        */
        rc = sqlite3_exec(DBaseInstance, "CREATE TABLE " QPSKPARAMS_TABLE " ( "
                          QPSKPARAM_MULTIPLEXUID " PRIMARY KEY,"
                          QPSKPARAM_FREQ ","
                          QPSKPARAM_INVERSION ","
                          QPSKPARAM_SYMBOL_RATE ","
                          QPSKPARAM_FEC_INNER ","
                          QPSKPARAM_TONE ","
                          QPSKPARAM_POLARISATION ","
                          QPSKPARAM_SATNUMBER
                          ");", NULL, NULL, NULL);
        if (rc)
        {
            LogModule(LOG_ERROR, DBASE, "Failed to create QPSKParameters table: %s\n", sqlite3_errmsg(DBaseInstance));
            return rc;
        }
        /*
        (DVBC) QAM: <channel name>:<frequency>:<inversion>:<sym_rate>:<fec>:<qam>
        */

        rc = sqlite3_exec(DBaseInstance, "CREATE TABLE " QAMPARAMS_TABLE " ( "
                          QAMPARAM_MULTIPLEXUID " PRIMARY KEY,"
                          QAMPARAM_FREQ ","
                          QAMPARAM_INVERSION ","
                          QAMPARAM_SYMBOL_RATE ","
                          QAMPARAM_FEC_INNER ","
                          QAMPARAM_MODULATION
                          ");", NULL, NULL, NULL);
        if (rc)
        {
            LogModule(LOG_ERROR, DBASE, "Failed to create QAMParameters table: %s\n", sqlite3_errmsg(DBaseInstance));
            return rc;
        }
        /*
        (ATSC) VSB:  <channel name>:<frequency>:<modulation>
        */
        rc = sqlite3_exec(DBaseInstance, "CREATE TABLE " VSBPARAMS_TABLE " ( "
                          VSBPARAM_MULTIPLEXUID " PRIMARY KEY,"
                          VSBPARAM_FREQ ","
                          VSBPARAM_MODULATION
                          ");", NULL, NULL, NULL);
        if (rc)
        {
            LogModule(LOG_ERROR, DBASE, "Failed to create VSBParameters table: %s\n", sqlite3_errmsg(DBaseInstance));
            return rc;
        }

    }

    DBaseMetadataSetDouble(METADATA_DBASE_VERSION,DBASE_VERSION);

    sqlite3_exec(DBaseInstance, "COMMIT TRANSACTION;", NULL, NULL, NULL);
                                                 
    return rc;
}


int DBaseMetadataGet(char *name, char **value)
{
    STATEMENT_INIT;
    STATEMENT_PREPAREVA("SELECT " METADATA_VALUE " "
                        "FROM " METADATA_TABLE " "
                        "WHERE " METADATA_NAME "='%q';", name);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();
    RETURN_RC_ON_ERROR;
    
    *value = strdup(STATEMENT_COLUMN_TEXT(0));
    STATEMENT_FINALIZE();
    return rc;
}

int DBaseMetadataSet(char *name, char *value)
{
    STATEMENT_INIT;
    STATEMENT_PREPAREVA("INSERT OR REPLACE INTO " METADATA_TABLE " "
                        "(" METADATA_NAME "," METADATA_VALUE ") "
                        "VALUES('%q','%q');", name, value);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();
    RETURN_RC_ON_ERROR;
    
    STATEMENT_FINALIZE();
    return rc;
}

int DBaseMetadataGetInt(char *name, int *value)
{
    STATEMENT_INIT;
    STATEMENT_PREPAREVA("SELECT " METADATA_VALUE " "
                        "FROM " METADATA_TABLE " "
                        "WHERE " METADATA_NAME "='%q';", name);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();
    RETURN_RC_ON_ERROR;
    
    *value = STATEMENT_COLUMN_INT(0);
    STATEMENT_FINALIZE();
    return rc;
}
int DBaseMetadataSetInt(char *name, int value)
{
    STATEMENT_INIT;
    STATEMENT_PREPAREVA("INSERT OR REPLACE INTO " METADATA_TABLE " "
                        "(" METADATA_NAME "," METADATA_VALUE ") "
                        "VALUES('%q',%d);", name, value);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();
    RETURN_RC_ON_ERROR;
    STATEMENT_FINALIZE();
    return rc;
}

int DBaseMetadataGetDouble(char *name, double *value)
{
    STATEMENT_INIT;
    STATEMENT_PREPAREVA("SELECT " METADATA_VALUE " "
                        "FROM " METADATA_TABLE " "
                        "WHERE " METADATA_NAME "='%q';", name);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();
    RETURN_RC_ON_ERROR;
    *value = STATEMENT_COLUMN_DOUBLE(0);
    STATEMENT_FINALIZE();
    return rc;
}

int DBaseMetadataSetDouble(char *name, double value)
{
    STATEMENT_INIT;
    STATEMENT_PREPAREVA("INSERT OR REPLACE INTO " METADATA_TABLE " "
                        "(" METADATA_NAME "," METADATA_VALUE ") "
                        "VALUES('%q',%e);", name, value);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();
    RETURN_RC_ON_ERROR;
    STATEMENT_FINALIZE();
    return rc;
}

int DBaseMetadataDelete(char *name)
{
    STATEMENT_INIT;
    STATEMENT_PREPAREVA("DELETE FROM " METADATA_TABLE " "
                        "WHERE " METADATA_NAME "='%q';", name);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();
    RETURN_RC_ON_ERROR;

    STATEMENT_FINALIZE();
    return rc;
}

