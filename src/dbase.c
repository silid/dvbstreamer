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
#include <sys/stat.h>
#include <sys/types.h>

#include "dbase.h"
#include "main.h"
#include "logging.h"

#define VERSION 0.1

sqlite3 *DBaseInstance;

int DBaseCreateTables();
int DBaseCheckVersion();

int DBaseInit(int adapter)
{
	char dir[PATH_MAX];
	char file[PATH_MAX];
	int rc;
	
	sprintf(dir, "%s/.dvbstreamer", getenv("HOME"));
	mkdir(dir, S_IRWXU);
	
	sprintf(file, "%s/adapter%d.db", dir, adapter);
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
	int rc;
	char **results;
	int nrow, ncolumn;
	rc = sqlite3_get_table(DBaseInstance, "select version from Version;",  &results, &nrow, &ncolumn, NULL);
	if (rc)
	{
		return DBaseCreateTables();
	}
	/* Check version number and upgrade tables for future releases ? */
	
	sqlite3_free_table(results);
	return rc;
}

int DBaseCreateTables()
{
	int rc;
	
	printlog(LOG_DEBUG, "Creating tables\n");
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
		<frequency>    = unsigned long
		
		
		<inversion>    = INVERSION_ON | INVERSION_OFF | INVERSION_AUTO
		<fec>          = FEC_1_2, FEC_2_3, FEC_3_4 .... FEC_AUTO ... FEC_NONE
		<qam>          = QPSK, QAM_128, QAM_16 ...

		<bw>           = BANDWIDTH_6_MHZ, BANDWIDTH_7_MHZ, BANDWIDTH_8_MHZ
		<fec_hp>       = <fec>
		<fec_lp>       = <fec>
		<transmissionm> = TRANSMISSION_MODE_2K, TRANSMISSION_MODE_8K
		
		489833330:INVERSION_AUTO:BANDWIDTH_8_MHZ:FEC_3_4:FEC_3_4:QAM_16:TRANSMISSION_MODE_2K:GUARD_INTERVAL_1_32:HIERARCHY_NONE:
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

	rc = sqlite3_exec(DBaseInstance, "INSERT INTO " VERSION_TABLE " VALUES ( 0.1 );", NULL, NULL, NULL);
	return rc;
}
