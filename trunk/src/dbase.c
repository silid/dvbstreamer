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
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>

#include "dbase.h"
#include "types.h"
#include "main.h"
#include "logging.h"
#include "objects.h"
#include "deferredproc.h"


/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/

static int DBaseCreateTables(void);
static int DBaseCheckVersion();


/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static sqlite3 *DBaseInstance;

static char DBASE[] = "dbase";
static char dbaseFile[PATH_MAX];
static pthread_key_t dbaseKey;
/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
int DBaseInit(int adapter)
{
    int rc;

    pthread_key_create(&dbaseKey, (void(*)(void *))sqlite3_close);

    sprintf(dbaseFile, "%s/adapter%d.db", DataDirectory, adapter);
    rc = sqlite3_open(dbaseFile, &DBaseInstance);
    if (rc)
    {
        LogModule(LOG_ERROR, DBASE, "Can't open database: %s\n", sqlite3_errmsg(DBaseInstance));
        sqlite3_close(DBaseInstance);
    }
    else
    {
        pthread_setspecific(dbaseKey, (void*)DBaseInstance);
        sqlite3_busy_timeout(DBaseInstance, 500);
        rc = DBaseCheckVersion();
    }
    
    return rc;
}


void DBaseDeInit()
{
    pthread_setspecific(dbaseKey, NULL);
    sqlite3_close(DBaseInstance);
}

sqlite3* DBaseConnectionGet(void)
{
    
    sqlite3 *connection = pthread_getspecific(dbaseKey);
    if (connection == NULL)
    {
        int rc = sqlite3_open(dbaseFile, &connection);
        if (rc)
        {
            LogModule(LOG_ERROR, DBASE, "Can't open database: %s\n", sqlite3_errmsg(connection));
            sqlite3_close(connection);
            connection = NULL;
        }
        else
        {
            LogModule(LOG_DEBUG, DBASE, "Database opened successfully. (%p)\n", connection);
            sqlite3_busy_timeout(connection, 500);            
            pthread_setspecific(dbaseKey, (void*)connection);
        }
    }
    return connection;
}

int DBaseTransactionBegin(void)
{
    sqlite3 *connection = DBaseConnectionGet();
    return sqlite3_exec(connection, "BEGIN TRANSACTION;", NULL, NULL, NULL);
}

int DBaseTransactionCommit(void)
{
    sqlite3 *connection = DBaseConnectionGet();
    return sqlite3_exec(connection, "COMMIT TRANSACTION;", NULL, NULL, NULL);
}

int DBaseCount(char *table, char *where)
{
    STATEMENT_INIT;
    int result = -1;
    if (where == NULL)
    {
        STATEMENT_PREPAREVA("SELECT count() FROM %s;", table);
    }
    else
    {
        STATEMENT_PREPAREVA("SELECT count() FROM %s WHERE %s;", table, where);
    }
    RETURN_ON_ERROR(-1);

    STATEMENT_STEP();
    if (rc == SQLITE_ROW)
    {
        result = STATEMENT_COLUMN_INT(0);
        rc = 0;
    }
    STATEMENT_FINALIZE();
    return result;
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
        rc = DBaseCreateTables();
    }
    else
    {
        LogModule(LOG_DEBUG, DBASE, "Current version of database is %f\n", version);
        /* Check version number and upgrade tables for future releases ? */
        if (version < DBASE_VERSION)
        {
            int pid;
            char path[PATH_MAX];
            char *filename;
            ssize_t len;
            /* Determine the location of file for the running process */
            sprintf(path, "/proc/%d/exe", getpid());
            len = readlink(path, path, sizeof(path) - 1);
            path[len] = 0;
            /* Change the filename to point to the conversion program */
            filename = strrchr(path, '/');
            strcpy(filename + 1, "convertdvbdb");
            pid = fork();
            if (pid)
            {
                waitpid(pid, &rc, 0);
            }
            else
            {
                execl(path, path, dbaseFile, NULL);
                exit(1);
            }
        }
    }
    return rc;
}

static int DBaseCreateTables(void)
{
    int rc = 0;

    LogModule(LOG_DEBUG, DBASE, "Creating tables\n");
    sqlite3_exec(DBaseInstance, "BEGIN TRANSACTION;", NULL, NULL, NULL);

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
                      SERVICE_SOURCE ","
                      SERVICE_CA ","
                      SERVICE_TYPE ","
                      SERVICE_NAME ","
                      SERVICE_PMTPID " DEFAULT -1,"
                      SERVICE_PMTVERSION " DEFAULT -1,"
                      SERVICE_PCRPID " DEFAULT -1,"
                      SERVICE_PROVIDER ","
                      SERVICE_DEFAUTHORITY ","
                      "PRIMARY KEY ( "SERVICE_MULTIPLEXUID "," SERVICE_ID ")"
                      ");", NULL, NULL, NULL);
    if (rc)
    {
        LogModule(LOG_ERROR, DBASE, "Failed to create Services table: %s\n", sqlite3_errmsg(DBaseInstance));
        return rc;
    }

    rc = sqlite3_exec(DBaseInstance, "CREATE TABLE " MULTIPLEXES_TABLE " ( "
                      MULTIPLEX_UID " INTEGER PRIMARY KEY,"
                      MULTIPLEX_TYPE ","
                      MULTIPLEX_TSID " DEFAULT -1,"
                      MULTIPLEX_NETID" DEFAULT -1,"
                      MULTIPLEX_TUNINGPARAMS
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
        LogModule(LOG_ERROR, DBASE, "Failed to create PIDs table: %s\n", sqlite3_errmsg(DBaseInstance));
        return rc;
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

