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

/* This is the version of the database not the application!*/
#define DBASE_VERSION 0.4

sqlite3 *DBaseInstance;

int DBaseCreateTables(double version);
int DBaseCheckVersion();

int DBaseInit(int adapter)
{
    char file[PATH_MAX];
    int rc;

    sprintf(file, "%s/adapter%d.db", DataDirectory, adapter);
    rc = sqlite3_open(file, &DBaseInstance);
    if( rc )
    {
        printlog(LOG_ERROR, "Can't open database: %s\n", sqlite3_errmsg(DBaseInstance));
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

int DBaseCheckVersion()
{
    STATEMENT_INIT;
	double version;
	STATEMENT_PREPARE("select " VERSION_VERSION " from " VERSION_TABLE ";");
    STATEMENT_STEP();
    if ((rc != SQLITE_OK) && (rc != SQLITE_ROW) && (rc != SQLITE_DONE))
    {
		printlog(LOG_DEBUG, "Failed to get contents of version table (%d)\n", rc);
		version = 0.0f;
    }
	else
	{
		version = STATEMENT_COLUMN_DOUBLE(0);
	}
	STATEMENT_FINALIZE();
	printlog(LOG_DEBUG, "Current version of database is %f\n", version);
    /* Check version number and upgrade tables for future releases ? */
	rc = DBaseCreateTables(version);
    return rc;
}

int DBaseCreateTables(double version)
{
    int rc;

    printlog(LOG_DEBUG, "Creating tables\n");
	/* Version 0.1 - 0.2 tables */
	if (version < 0.1)
	{
	    rc = sqlite3_exec(DBaseInstance, "CREATE TABLE " SERVICES_TABLE " ( "
	                      SERVICE_MPLEXFREQ ","
	                      SERVICE_ID ","
	                      SERVICE_NAME ","
	                      SERVICE_PMTPID ","
	                      SERVICE_PMTVERSION ","
	                      "PRIMARY KEY ( "SERVICE_MPLEXFREQ "," SERVICE_ID ")"
	                      ");", NULL, NULL, NULL);
	    if (rc)
	    {
	        printlog(LOG_ERROR, "Failed to create Services table: %s\n", sqlite3_errmsg(DBaseInstance));
	        return rc;
	    }

	    rc = sqlite3_exec(DBaseInstance, "CREATE TABLE " MULTIPLEXES_TABLE " ( "
	                      MULTIPLEX_FREQ " PRIMARY KEY,"
	                      MULTIPLEX_ID ","
	                      MULTIPLEX_TYPE ","
	                      MULTIPLEX_PATVERSION
	                      ");", NULL, NULL, NULL);
	    if (rc)
	    {
	        printlog(LOG_ERROR, "Failed to create Multiplexes table: %s\n", sqlite3_errmsg(DBaseInstance));
	        return rc;
	    }
	    /*
	    (DVBT) OFDM: <frequency>:<inversion>:<bw>:<fec_hp>:<fec_lp>:<qam>:<transmissionm>:<guardlist>:<hierarchinfo>
	    */
	    rc = sqlite3_exec(DBaseInstance, "CREATE TABLE " OFDMPARAMS_TABLE " ( "
	                      OFDMPARAM_FREQ " PRIMARY KEY,"
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
	        printlog(LOG_ERROR, "Failed to create OFDMParameters table: %s\n", sqlite3_errmsg(DBaseInstance));
	        return rc;
	    }

	    rc = sqlite3_exec(DBaseInstance, "CREATE TABLE " PIDS_TABLE " ( "
	                      PID_MPLEXFREQ ","
	                      PID_SERVICEID ","
	                      PID_PID ","
	                      PID_TYPE ","
	                      PID_SUBTYPE ","
	                      PID_PMTVERSION ","
	                      "PRIMARY KEY(" PID_MPLEXFREQ "," PID_SERVICEID "," PID_PID ")"
	                      ");", NULL, NULL, NULL);
	    if (rc)
	    {
	        printlog(LOG_ERROR, "Failed to create OFDMParameters table: %s\n", sqlite3_errmsg(DBaseInstance));
	        return rc;
	    }

	    rc = sqlite3_exec(DBaseInstance, "CREATE TABLE " VERSION_TABLE " ( "
	                      VERSION_VERSION
	                      ");", NULL, NULL, NULL);
	    if (rc)
	    {
	        printlog(LOG_ERROR, "Failed to create Version table: %s\n", sqlite3_errmsg(DBaseInstance));
	        return rc;
	    }
	}
	/* Version 0.2 - 0.3 tables */
	if (version < 0.2)
	{
		/*
		(DVBS) QPSK: <channel name>:<frequency>:<polarisation>:<sat_no>:<sym_rate>
		(DVBC) QAM: <channel name>:<frequency>:<inversion>:<sym_rate>:<fec>:<qam>
		*/
		rc = sqlite3_exec(DBaseInstance, "CREATE TABLE " QPSKPARAMS_TABLE " ( "
	                      QPSKPARAM_FREQ " PRIMARY KEY,"
	                      QPSKPARAM_INVERSION ","
	                      QPSKPARAM_SYMBOL_RATE ","
	                      QPSKPARAM_FEC_INNER
	                      ");", NULL, NULL, NULL);
	    if (rc)
	    {
	        printlog(LOG_ERROR, "Failed to create OFDMParameters table: %s\n", sqlite3_errmsg(DBaseInstance));
	        return rc;
	    }
		rc = sqlite3_exec(DBaseInstance, "CREATE TABLE " QAMPARAMS_TABLE " ( "
	                      QAMPARAM_FREQ " PRIMARY KEY,"
	                      QAMPARAM_INVERSION ","
	                      QAMPARAM_SYMBOL_RATE ","
	                      QAMPARAM_FEC_INNER ","
	                      QAMPARAM_MODULATION
	                      ");", NULL, NULL, NULL);
	    if (rc)
	    {
	        printlog(LOG_ERROR, "Failed to create OFDMParameters table: %s\n", sqlite3_errmsg(DBaseInstance));
	        return rc;
	    }
	}

	/* Version 0.3 tables */
	if (version < 0.3)
	{
	    rc = sqlite3_exec(DBaseInstance, "ALTER TABLE " MULTIPLEXES_TABLE " ADD " MULTIPLEX_NETID ";", NULL, NULL, NULL);

	    if (rc)
	    {
	        printlog(LOG_ERROR, "Failed to add network id column to multiplexes table: %s\n", sqlite3_errmsg(DBaseInstance));
	        return rc;
	    }
	}

	if (version < 0.4)
	{
	    rc = sqlite3_exec(DBaseInstance, "ALTER TABLE " SERVICES_TABLE " ADD " SERVICE_PCRPID " DEFAULT -1;", NULL, NULL, NULL);

	    if (rc)
	    {
	        printlog(LOG_ERROR, "Failed to add pcrpid column to services table: %s\n", sqlite3_errmsg(DBaseInstance));
	        return rc;
	    }

	    rc = sqlite3_exec(DBaseInstance, "ALTER TABLE " PIDS_TABLE " ADD " PID_DESCRIPTORS " DEFAULT NULL;", NULL, NULL, NULL);

	    if (rc)
	    {
	        printlog(LOG_ERROR, "Failed to add descriptors column to pids table: %s\n", sqlite3_errmsg(DBaseInstance));
	        return rc;
	    }

	}

    rc = sqlite3_exec(DBaseInstance, "DELETE FROM " VERSION_TABLE ";", NULL, NULL, NULL);
	rc = sqlite3_exec(DBaseInstance, "INSERT INTO " VERSION_TABLE " VALUES ( " TOSTRING(DBASE_VERSION) " );", NULL, NULL, NULL);
    return rc;
}
