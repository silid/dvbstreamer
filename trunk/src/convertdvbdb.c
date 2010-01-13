#include <stdio.h>
#include <stdlib.h>

#include "dbase.h"
#include "main.h"
#include "logging.h"

/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/

/* v0.6 defines */
#define OFDMPARAMS_TABLE        "OFDMParameters"
#define OFDMPARAM_MULTIPLEXUID  "mplexuid"
#define OFDMPARAM_FREQ          "freq"
#define OFDMPARAM_INVERSION     "inversion"
#define OFDMPARAM_BW            "bw"
#define OFDMPARAM_FEC_HP        "fec_hp"
#define OFDMPARAM_FEC_LP        "fec_lp"
#define OFDMPARAM_QAM           "qam"
#define OFDMPARAM_TRANSMISSIONM "transmissionm"
#define OFDMPARAM_GUARDLIST     "guardlist"
#define OFDMPARAM_HIERARCHINFO  "hierarchinfo"
#define QPSKPARAMS_TABLE  "QPSKParameters"
#define QPSKPARAM_MULTIPLEXUID  "mplexuid"
#define QPSKPARAM_FREQ          "freq"
#define QPSKPARAM_INVERSION     "inversion"
#define QPSKPARAM_SYMBOL_RATE    "symbol_rate"
#define QPSKPARAM_FEC_INNER      "fec_inner"
#define QPSKPARAM_POLARISATION   "polarisation"
#define QPSKPARAM_SATNUMBER      "satnumber"
#define QAMPARAMS_TABLE   "QAMParameters"
#define QAMPARAM_MULTIPLEXUID  "mplexuid"
#define QAMPARAM_FREQ           "freq"
#define QAMPARAM_INVERSION      "inversion"
#define QAMPARAM_SYMBOL_RATE    "symbol_rate"
#define QAMPARAM_FEC_INNER      "fec_inner"
#define QAMPARAM_MODULATION     "modulation"
#define VSBPARAMS_TABLE   "VSBParameters"
#define VSBPARAM_MULTIPLEXUID  "mplexuid"
#define VSBPARAM_FREQ           "freq"
#define VSBPARAM_MODULATION     "modulation"

/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/
struct Converter_s{
    double version;
    int (*convert)(void);
};

typedef struct Param_s
{
    char *str;
    int value;
}Param_t;

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static int UpdateDatabaseVersion(void);
static char *findParameter(const Param_t *params, int value);

static int convert0_6(void);


/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static sqlite3 *connection;

static const struct Converter_s converters[]= {
    { 0.6, convert0_6},
};

static const Param_t inversion_list [] =
    {
        { "OFF", INVERSION_OFF },
        { "ON", INVERSION_ON },
        { "AUTO", INVERSION_AUTO },
        { NULL, 0 }
    };

static const Param_t bw_list [] =
    {
        { "6Mhz", BANDWIDTH_6_MHZ },
        { "7Mhz", BANDWIDTH_7_MHZ },
        { "8Mhz", BANDWIDTH_8_MHZ },
        { "AUTO",    BANDWIDTH_AUTO },
        { NULL, 0 }
    };

static const Param_t fec_list [] =
    {
        { "AUTO", FEC_AUTO },
        { "1/2", FEC_1_2 },
        { "2/3", FEC_2_3 },
        { "3/4", FEC_3_4 },
        { "4/5", FEC_4_5 },
        { "5/6", FEC_5_6 },
        { "6/7", FEC_6_7 },
        { "7/8", FEC_7_8 },
        { "8/9", FEC_8_9 },
        { "NONE", FEC_NONE },
        { NULL, 0 }
    };

static const Param_t guard_list [] =
    {
        {"1/16", GUARD_INTERVAL_1_16 },
        {"1/32", GUARD_INTERVAL_1_32 },
        {"1/4", GUARD_INTERVAL_1_4 },
        {"1/8", GUARD_INTERVAL_1_8 },
        {"AUTO", GUARD_INTERVAL_AUTO },
        { NULL, 0 }
    };

static const Param_t hierarchy_list [] =
    {
        { "NONE", HIERARCHY_NONE },
        { "1", HIERARCHY_1 },
        { "2", HIERARCHY_2 },
        { "4", HIERARCHY_4 },
        { "AUTO", HIERARCHY_AUTO },            
        { NULL, 0 }
    };

static const Param_t modulation_list [] =
    {
        { "QPSK", QPSK },
        { "16QAM", QAM_16 },
        { "32QAM", QAM_32 },
        { "64QAM", QAM_64 },
        { "128QAM", QAM_128 },
        { "256QAM", QAM_256 },
        { "AUTO", QAM_AUTO },
        { "8VSB", VSB_8 },
        { "16VSB", VSB_16 },
        { NULL, 0 }
    };

static const Param_t transmissionmode_list [] =
    {
        { "2K", TRANSMISSION_MODE_2K },
        { "8K", TRANSMISSION_MODE_8K },
        { "AUTO", TRANSMISSION_MODE_AUTO },
        { NULL, 0 }
    };

static const Param_t polarisation_list[] = 
    {
        {"Horizontal", 0},
        {"Vertical", 1},
        { NULL, 0}
    };

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
int main(int argc, char *argv[])
{
    sqlite3_stmt *stmt;
    int rc;
    double version;
    int i;
    
    if (argc < 2)
    {
        fprintf(stderr, "Missing database file to convert!\n");
        return 1;
    }
    rc = sqlite3_open(argv[1], &connection);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Can\'t open database: %s\n", sqlite3_errmsg(connection));
        sqlite3_close(connection);
        return 1;
    }


    rc = sqlite3_prepare( connection,  "SELECT " METADATA_VALUE " "
                        "FROM " METADATA_TABLE " "
                        "WHERE " METADATA_NAME "=\"" METADATA_DBASE_VERSION "\";", -1, &stmt, NULL);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Failed to prepare version statement: %s\n", sqlite3_errmsg(connection));
        return 1;
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW)
    {
        fprintf(stderr, "Failed to retrieve version: %s\n", sqlite3_errmsg(connection));
        return 1;        
    }
    version = sqlite3_column_double(stmt, 0);
    sqlite3_finalize(stmt);
    printf("Current version of database is %.2f\n", version);

    for (i = 0; i < sizeof(converters)/sizeof(struct Converter_s); i ++)
    {
        if (version <= converters[i].version)
        {
            rc = converters[i].convert();
            if (rc)
            {
                break;
            }
        }
    }
    if (rc == SQLITE_OK)
    {
        UpdateDatabaseVersion();
    }
    sqlite3_close(connection);
    return 0;
}

/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/
static int convert0_6(void)
{
    int rc;
    sqlite3_stmt *stmt;
    char **muxTable;
    char *errMsg;
    int rows, columns;
    int m;
    char statement[256];
    char buffer[256];
    char *update;
    
    rc = sqlite3_exec(connection, "ALTER TABLE " MULTIPLEXES_TABLE " ADD " MULTIPLEX_TUNINGPARAMS ";", NULL, NULL, NULL);
    
    if (rc)
    {
        fprintf(stderr, "Failed to add tuning parameters column to mulitplexes table: %s\n", sqlite3_errmsg(connection));
        return rc;
    }

    rc = sqlite3_get_table(connection, "SELECT " MULTIPLEX_UID "," MULTIPLEX_TYPE " FROM " MULTIPLEXES_TABLE ";", &muxTable, &rows, &columns, &errMsg);
    if (rc)
    {
        fprintf(stderr, "Failed to retrieve multiplexes: %s\n", errMsg);
        sqlite3_free(errMsg);
        return rc;
    }
    
    for (m = 1; m < rows+1; m ++)
    {
        switch(muxTable[(m * 2) + 1][0])
        {
            case '0':
                /* QPSK */
                sprintf(statement, "SELECT " QPSKPARAM_FREQ "," QPSKPARAM_INVERSION "," QPSKPARAM_FEC_INNER "," 
                                             QPSKPARAM_SYMBOL_RATE "," QPSKPARAM_POLARISATION "," QPSKPARAM_SATNUMBER
                                    " FROM " QPSKPARAMS_TABLE 
                                    " WHERE "QPSKPARAM_MULTIPLEXUID "=%s;", muxTable[m*2]);
                rc = sqlite3_prepare(connection, statement, -1, &stmt, NULL);
                if (rc == SQLITE_OK)
                {
                    char *pol, *inversion, *fec;
                    rc = sqlite3_step(stmt);
                    inversion = findParameter(inversion_list, sqlite3_column_int(stmt, 1));
                    fec = findParameter(fec_list, sqlite3_column_int(stmt, 2));
                    pol = findParameter(polarisation_list, sqlite3_column_int(stmt, 4));
                    sprintf(buffer, "Frequency: %d\n"
                                    "FEC: %s\n"
                                    "Inversion: %s\n"
                                    "Symbol Rate: %d\n"
                                    "Polarisation: %s\n"
                                    "Satellite Number: %d\n",
                                        sqlite3_column_int(stmt, 0),
                                        fec,
                                        inversion,
                                        sqlite3_column_int(stmt, 3),
                                        pol,
                                        sqlite3_column_int(stmt, 5));
                    sqlite3_finalize(stmt);
                    
                }
                else
                {
                    fprintf(stderr, "Failed prepare qpsk select: %s\n", sqlite3_errmsg(connection));                    
                }
                
                break;
            case '1':
                /* QAM */
                sprintf(statement, "SELECT " QAMPARAM_FREQ "," QAMPARAM_INVERSION "," QAMPARAM_FEC_INNER "," QAMPARAM_MODULATION "," QAMPARAM_SYMBOL_RATE 
                                    " FROM " QAMPARAMS_TABLE " WHERE " QAMPARAM_MULTIPLEXUID "=%s;", muxTable[m*2]);
                rc = sqlite3_prepare(connection, statement, -1, &stmt, NULL);
                if (rc == SQLITE_OK)
                {
                    char *mod, *inversion, *fec;
                    rc = sqlite3_step(stmt);
                    inversion = findParameter(inversion_list, sqlite3_column_int(stmt, 1));
                    fec = findParameter(fec_list, sqlite3_column_int(stmt, 2));
                    mod = findParameter(modulation_list, sqlite3_column_int(stmt, 3));
                    sprintf(buffer, "Frequency: %d\n"
                                    "FEC: %s\n"
                                    "Inversion: %s\n"
                                    "Symbol Rate: %d\n"
                                    "Modulation: %s\n",
                                        sqlite3_column_int(stmt, 0),
                                        fec,
                                        inversion,
                                        sqlite3_column_int(stmt, 4),
                                        mod);
                    sqlite3_finalize(stmt);
                    
                }
                else
                {
                    fprintf(stderr, "Failed prepare qam select: %s\n", sqlite3_errmsg(connection));                    
                }
                break;
            case '2':
                /* OFDM */
                sprintf(statement, "SELECT " OFDMPARAM_FREQ "," OFDMPARAM_INVERSION "," OFDMPARAM_BW "," OFDMPARAM_FEC_LP "," OFDMPARAM_FEC_HP "," 
                                             OFDMPARAM_QAM "," OFDMPARAM_TRANSMISSIONM "," OFDMPARAM_GUARDLIST "," OFDMPARAM_HIERARCHINFO
                                    " FROM " OFDMPARAMS_TABLE " WHERE " OFDMPARAM_MULTIPLEXUID "=%s;", muxTable[m*2]);
                rc = sqlite3_prepare(connection, statement, -1, &stmt, NULL);
                if (rc == SQLITE_OK)
                {
                    char *mod, *inversion, *bw, *fec_lp, *fec_hp, *transmission, *guardlist, *hierarchy;
                    rc = sqlite3_step(stmt);
                    inversion = findParameter(inversion_list, sqlite3_column_int(stmt, 1));
                    bw = findParameter(bw_list, sqlite3_column_int(stmt, 2));
                    fec_lp = findParameter(fec_list, sqlite3_column_int(stmt, 3));
                    fec_hp = findParameter(fec_list, sqlite3_column_int(stmt, 4));
                    mod = findParameter(modulation_list, sqlite3_column_int(stmt, 5));
                    transmission = findParameter(transmissionmode_list, sqlite3_column_int(stmt, 6));
                    guardlist = findParameter(modulation_list, sqlite3_column_int(stmt, 7));
                    hierarchy = findParameter(hierarchy_list, sqlite3_column_int(stmt, 9));

                    sprintf(buffer, "Frequency: %d\n"
                                    "Inversion: %s\n"
                                    "Bandwidth: %s\n"
                                    "FEC LP: %s\n"
                                    "FEC HP: %s\n"
                                    "Constellation: %s\n"
                                    "Transmission Mode: %s\n"
                                    "Guard Interval: %s\n"
                                    "Hierarchy: %s\n",
                                        sqlite3_column_int(stmt, 0),
                                        inversion, bw, fec_lp, fec_hp,
                                        mod, transmission, guardlist, hierarchy);
                    sqlite3_finalize(stmt);
                }
                else
                {
                    fprintf(stderr, "Failed prepare ofdm select: %s\n", sqlite3_errmsg(connection));                    
                }

                break;
            case '3':
                /* ATSC */
                sprintf(statement, "SELECT " VSBPARAM_FREQ "," VSBPARAM_MODULATION
                                    " FROM " VSBPARAMS_TABLE " WHERE " VSBPARAM_MULTIPLEXUID "=%s;", muxTable[m*2]);
                rc = sqlite3_prepare(connection, statement, -1, &stmt, NULL);
                if (rc == SQLITE_OK)
                {
                    char *mod;
                    rc = sqlite3_step(stmt);
                    mod = findParameter(modulation_list, sqlite3_column_int(stmt, 1));
                    sprintf(buffer, "Frequency: %d\n"
                                    "Modulation: %s\n",
                                    sqlite3_column_int(stmt, 0), mod);
                    sqlite3_finalize(stmt);
                    
                }
                else
                {
                    fprintf(stderr, "Failed prepare vsb select: %s\n", sqlite3_errmsg(connection));                    
                }
                break;
            default:
                break;
        }
        update = sqlite3_mprintf("UPDATE " MULTIPLEXES_TABLE " SET " MULTIPLEX_TUNINGPARAMS "=%Q WHERE " MULTIPLEX_UID"=%s;",
                                buffer, muxTable[m*2]);
        rc = sqlite3_prepare(connection, 
                        update, -1, &stmt, NULL);
        if (rc == SQLITE_OK)
        {
            sqlite3_step(stmt);
        }
        sqlite3_finalize(stmt);
        sqlite3_free(update);
    }
    sqlite3_free_table(muxTable);

    rc = sqlite3_exec(connection, "DROP TABLE " OFDMPARAMS_TABLE ";", NULL, NULL, NULL);
    rc = sqlite3_exec(connection, "DROP TABLE " QPSKPARAMS_TABLE ";", NULL, NULL, NULL);
    rc = sqlite3_exec(connection, "DROP TABLE " QAMPARAMS_TABLE ";", NULL, NULL, NULL);
    rc = sqlite3_exec(connection, "DROP TABLE " VSBPARAMS_TABLE ";", NULL, NULL, NULL);
    return rc;    
}

static int UpdateDatabaseVersion(void)
{
    char statement[256];
    sprintf(statement, "INSERT OR REPLACE INTO " METADATA_TABLE " "
                        "(" METADATA_NAME "," METADATA_VALUE ") "
                        "VALUES('" METADATA_DBASE_VERSION "',%e);", DBASE_VERSION);
    return sqlite3_exec(connection, statement, NULL, NULL, NULL);
}

static char *findParameter(const Param_t *params, int value)
{
    int i;
    for (i = 0; params[i].str; i ++)
    {
        if (params[i].value == value)
        {
            return params[i].str;
        }
    }
    return NULL;
}

