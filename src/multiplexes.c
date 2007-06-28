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

multiplexes.c

Manage multiplexes and tuning parameters.

*/
#include <stdlib.h>
#include <string.h>

#include "dbase.h"
#include "multiplexes.h"
#include "logging.h"

/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/
#define MULTIPLEX_FIELDS MULTIPLEX_UID "," MULTIPLEX_FREQ "," MULTIPLEX_TSID "," \
                        MULTIPLEX_NETID "," MULTIPLEX_TYPE "," MULTIPLEX_PATVERSION
                        
/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/

static int OFDMParametersGet(int uid, struct dvb_frontend_parameters *feparams);
static int OFDMParametersAdd(int uid,struct dvb_frontend_parameters *feparams);
static int QPSKParametersGet(int uid, struct dvb_frontend_parameters *feparams, DVBDiSEqCSettings_t *diseqc);
static int QPSKParametersAdd(int uid,struct dvb_frontend_parameters *feparams, DVBDiSEqCSettings_t *diseqc);
static int QAMParametersGet(int uid, struct dvb_frontend_parameters *feparams);
static int QAMParametersAdd(int uid,struct dvb_frontend_parameters *feparams);
static int VSBParametersGet(int uid, struct dvb_frontend_parameters *feparams);
static int VSBParametersAdd(int uid,struct dvb_frontend_parameters *feparams);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static int uidSeed;

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/

int MultiplexInit(void)
{
    uidSeed = (int)time(NULL);
    return  ObjectRegisterType(Multiplex_t);
}

int MultiplexDeinit(void)
{
    return 0;
}

int MultiplexCount()
{
    STATEMENT_INIT;
    int result = -1;

    STATEMENT_PREPARE("SELECT count() FROM " MULTIPLEXES_TABLE ";");
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

Multiplex_t *MultiplexFind(int uid)
{
    Multiplex_t *result = NULL;
    STATEMENT_INIT;

    STATEMENT_PREPAREVA("SELECT " MULTIPLEX_FIELDS " "
                        "FROM " MULTIPLEXES_TABLE " WHERE " MULTIPLEX_UID "=%d;",uid);
    RETURN_ON_ERROR(NULL);

    result = MultiplexGetNext((MultiplexEnumerator_t)stmt);

    STATEMENT_FINALIZE();
    return result;
}

Multiplex_t *MultiplexFindId(int netid, int tsid)
{
    Multiplex_t *result = NULL;
    STATEMENT_INIT;

    STATEMENT_PREPAREVA("SELECT " MULTIPLEX_FIELDS " "
                        "FROM " MULTIPLEXES_TABLE
                        " WHERE " MULTIPLEX_NETID "=%d AND " MULTIPLEX_TSID "=%d;",
                        netid, tsid);
    RETURN_ON_ERROR(NULL);

    result = MultiplexGetNext((MultiplexEnumerator_t)stmt);

    STATEMENT_FINALIZE();
    return result;
}

Multiplex_t *MultiplexFindFrequency(int freq)
{
    Multiplex_t *result = NULL;
    STATEMENT_INIT;

    STATEMENT_PREPAREVA("SELECT " MULTIPLEX_FIELDS " "
                        "FROM " MULTIPLEXES_TABLE " WHERE " MULTIPLEX_FREQ"=%d;", freq);
    RETURN_ON_ERROR(NULL);

    result = MultiplexGetNext((MultiplexEnumerator_t)stmt);

    STATEMENT_FINALIZE();
    return result;
}

MultiplexEnumerator_t MultiplexEnumeratorGet()
{
    STATEMENT_INIT;
    STATEMENT_PREPARE("SELECT " MULTIPLEX_FIELDS " "
                      "FROM " MULTIPLEXES_TABLE ";");
    RETURN_ON_ERROR(NULL);
    return stmt;
}

void MultiplexEnumeratorDestroy(MultiplexEnumerator_t enumerator)
{
    int rc;
    sqlite3_stmt *stmt = (sqlite3_stmt *)enumerator;
    STATEMENT_FINALIZE();
}

Multiplex_t *MultiplexGetNext(MultiplexEnumerator_t enumerator)
{
    int rc;
    sqlite3_stmt *stmt = (sqlite3_stmt *)enumerator;

    STATEMENT_STEP();
    if (rc == SQLITE_ROW)
    {
        Multiplex_t *multiplex;
        multiplex = MultiplexNew();
        multiplex->uid = STATEMENT_COLUMN_INT(0);
        multiplex->freq = STATEMENT_COLUMN_INT(1);
        multiplex->tsId = STATEMENT_COLUMN_INT(2);
        multiplex->networkId = STATEMENT_COLUMN_INT(3);
        multiplex->type = STATEMENT_COLUMN_INT(4);
        multiplex->patVersion = STATEMENT_COLUMN_INT(5);

        return multiplex;
    }
    if (rc != SQLITE_DONE)
    {
        PRINTLOG_SQLITE3ERROR();
    }
    return NULL;
}

int MultiplexFrontendParametersGet(Multiplex_t *multiplex, struct dvb_frontend_parameters *feparams,  DVBDiSEqCSettings_t *diseqc)
{
    int result = -1;
    switch (multiplex->type)
    {
        case FE_QPSK:
            result = QPSKParametersGet(multiplex->uid, feparams, diseqc);
            break;
        case FE_QAM:
            result = QAMParametersGet(multiplex->uid, feparams);
            break;
        case FE_OFDM:
            result = OFDMParametersGet(multiplex->uid, feparams);
            break;
        case FE_ATSC:
            result = VSBParametersGet(multiplex->uid, feparams);
            break;
        default:
            break;
    }
    return result;
}

int MultiplexAdd(fe_type_t type, struct dvb_frontend_parameters *feparams, DVBDiSEqCSettings_t *diseqc, int *uid)
{
    STATEMENT_INIT;
    
    switch (type)
    {
        case FE_QPSK:
            rc = QPSKParametersAdd(uidSeed,feparams, diseqc);
            break;
        case FE_QAM:
            rc = QAMParametersAdd(uidSeed,feparams);
            break;
        case FE_OFDM:
            rc = OFDMParametersAdd(uidSeed,feparams);
            break;
        case FE_ATSC:
            rc = VSBParametersAdd(uidSeed,feparams);
            break;
        default:
            return -1;
    }
    RETURN_RC_ON_ERROR;
    STATEMENT_PREPAREVA("INSERT INTO " MULTIPLEXES_TABLE "("
                        MULTIPLEX_UID ","
                        MULTIPLEX_FREQ ","
                        MULTIPLEX_TYPE ")"
                        "VALUES (%d,%d,%d);", uidSeed, feparams->frequency, type);
    RETURN_RC_ON_ERROR;
    *uid = uidSeed;
    uidSeed ++;

    STATEMENT_STEP();

    STATEMENT_FINALIZE();
    return rc;
}

int MultiplexPATVersionSet(Multiplex_t *multiplex, int patversion)
{
    STATEMENT_INIT;
    STATEMENT_PREPAREVA("UPDATE " MULTIPLEXES_TABLE " "
                        "SET " MULTIPLEX_PATVERSION "=%d "
                        "WHERE " MULTIPLEX_UID "=%d;", patversion, multiplex->uid);
    multiplex->patVersion = patversion;
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();

    STATEMENT_FINALIZE();
    return rc;
}

int MultiplexTSIdSet(Multiplex_t *multiplex, int tsid)
{
    STATEMENT_INIT;
    STATEMENT_PREPAREVA("UPDATE " MULTIPLEXES_TABLE " "
                        "SET " MULTIPLEX_TSID "=%d "
                        "WHERE " MULTIPLEX_UID "=%d;", tsid, multiplex->uid);
    multiplex->tsId = tsid;
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();

    STATEMENT_FINALIZE();
    return rc;
}

int MultiplexNetworkIdSet(Multiplex_t *multiplex, int netid)
{
    STATEMENT_INIT;
    STATEMENT_PREPAREVA("UPDATE " MULTIPLEXES_TABLE " "
                        "SET " MULTIPLEX_NETID "=%d "
                        "WHERE " MULTIPLEX_UID "=%d;", netid, multiplex->uid);
    multiplex->networkId = netid;
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();

    STATEMENT_FINALIZE();
    return rc;
}

/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/

static int OFDMParametersGet(int uid, struct dvb_frontend_parameters *feparams)
{
    STATEMENT_INIT;
    STATEMENT_PREPAREVA("SELECT " OFDMPARAM_FREQ ","
                        OFDMPARAM_INVERSION ","
                        OFDMPARAM_BW ","
                        OFDMPARAM_FEC_HP ","
                        OFDMPARAM_FEC_LP ","
                        OFDMPARAM_QAM ","
                        OFDMPARAM_TRANSMISSIONM ","
                        OFDMPARAM_GUARDLIST ","
                        OFDMPARAM_HIERARCHINFO " "
                        "FROM " OFDMPARAMS_TABLE " WHERE " OFDMPARAM_MULTIPLEXUID"=%d;"
                        ,uid);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();
    if (rc == SQLITE_ROW)
    {
        feparams->frequency                    = STATEMENT_COLUMN_INT(0);
        feparams->inversion                    = STATEMENT_COLUMN_INT(1);
        feparams->u.ofdm.bandwidth             = STATEMENT_COLUMN_INT(2);
        feparams->u.ofdm.code_rate_HP          = STATEMENT_COLUMN_INT(3);
        feparams->u.ofdm.code_rate_LP          = STATEMENT_COLUMN_INT(4);
        feparams->u.ofdm.constellation         = STATEMENT_COLUMN_INT(5);
        feparams->u.ofdm.transmission_mode     = STATEMENT_COLUMN_INT(6);
        feparams->u.ofdm.guard_interval        = STATEMENT_COLUMN_INT(7);
        feparams->u.ofdm.hierarchy_information = STATEMENT_COLUMN_INT(8);
        rc = 0;
    }
    STATEMENT_FINALIZE();
    return rc;
}

static int OFDMParametersAdd(int uid, struct dvb_frontend_parameters *feparams)
{
    STATEMENT_INIT;
    STATEMENT_PREPAREVA("INSERT INTO " OFDMPARAMS_TABLE " "
                        "VALUES ("
                        "%d,"
                        "%d," /* OFDMPARAM_FREQ */
                        "%d," /* OFDMPARAM_INVERSION */
                        "%d," /* OFDMPARAM_BW */
                        "%d," /* OFDMPARAM_FEC_HP */
                        "%d," /* OFDMPARAM_FEC_LP */
                        "%d," /* OFDMPARAM_QAM */
                        "%d," /* OFDMPARAM_TRANSMISSIONM */
                        "%d," /* OFDMPARAM_GUARDLIST */
                        "%d"  /* OFDMPARAM_HIERARCHINFO */
                        ");",
                        uid,
                        feparams->frequency,
                        feparams->inversion,
                        feparams->u.ofdm.bandwidth,
                        feparams->u.ofdm.code_rate_HP,
                        feparams->u.ofdm.code_rate_LP,
                        feparams->u.ofdm.constellation,
                        feparams->u.ofdm.transmission_mode,
                        feparams->u.ofdm.guard_interval,
                        feparams->u.ofdm.hierarchy_information);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();

    STATEMENT_FINALIZE();
    return rc;
}

static int QPSKParametersGet(int uid, struct dvb_frontend_parameters *feparams, DVBDiSEqCSettings_t *diseqc)
{
    STATEMENT_INIT;
    STATEMENT_PREPAREVA("SELECT "
                        QPSKPARAM_FREQ ","
                        QPSKPARAM_INVERSION ","
                        QPSKPARAM_SYMBOL_RATE ","
                        QPSKPARAM_FEC_INNER ","
                        QPSKPARAM_POLARISATION ","
                        QPSKPARAM_SATNUMBER " "
                        "FROM " QPSKPARAMS_TABLE " WHERE " QPSKPARAM_MULTIPLEXUID"=%d;"
                        ,uid);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();
    if (rc == SQLITE_ROW)
    {
        feparams->frequency           = STATEMENT_COLUMN_INT(0);
        feparams->inversion           = STATEMENT_COLUMN_INT(1);
        feparams->u.qpsk.symbol_rate  = STATEMENT_COLUMN_INT(2);
        feparams->u.qpsk.fec_inner    = STATEMENT_COLUMN_INT(3);

        diseqc->polarisation     = STATEMENT_COLUMN_INT(4);
        diseqc->satellite_number = STATEMENT_COLUMN_INT(5);
        rc = 0;
    }
    STATEMENT_FINALIZE();
    return rc;
}

static int QPSKParametersAdd(int uid,struct dvb_frontend_parameters *feparams, DVBDiSEqCSettings_t *diseqc)
{
    STATEMENT_INIT;
    STATEMENT_PREPAREVA("INSERT INTO " QPSKPARAMS_TABLE " "
                        "VALUES ("
                        "%d,"
                        "%d," /* QPSKPARAM_FREQ */
                        "%d," /* QPSKPARAM_INVERSION */
                        "%d," /* QPSKPARAM_SYMBOL_RATE */
                        "%d," /* QPSKPARAM_FEC_INNER */
                        "%d," /* QPSKPARAM_POLARISATION */
                        "%d"  /* QPSKPARAM_SATNUMBER */
                        ");",
                        uid,
                        feparams->frequency,
                        feparams->inversion,
                        feparams->u.qpsk.symbol_rate,
                        feparams->u.qpsk.fec_inner,
                        diseqc->polarisation,
                        diseqc->satellite_number);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();

    STATEMENT_FINALIZE();
    return rc;
}

static int QAMParametersGet(int uid, struct dvb_frontend_parameters *feparams)
{
    STATEMENT_INIT;
    STATEMENT_PREPAREVA("SELECT "
                        QAMPARAM_FREQ ","
                        QAMPARAM_INVERSION ","
                        QAMPARAM_SYMBOL_RATE ","
                        QAMPARAM_FEC_INNER ","
                        QAMPARAM_MODULATION " "
                        "FROM " QAMPARAMS_TABLE " WHERE " QAMPARAM_MULTIPLEXUID"=%d;"
                        ,uid);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();
    if (rc == SQLITE_ROW)
    {
        feparams->frequency           = STATEMENT_COLUMN_INT(0);
        feparams->inversion           = STATEMENT_COLUMN_INT(1);
        feparams->u.qam.symbol_rate   = STATEMENT_COLUMN_INT(2);
        feparams->u.qam.fec_inner     = STATEMENT_COLUMN_INT(3);
        feparams->u.qam.modulation    = STATEMENT_COLUMN_INT(4);
        rc = 0;
    }
    STATEMENT_FINALIZE();
    return rc;
}

static int QAMParametersAdd(int uid,struct dvb_frontend_parameters *feparams)
{
    STATEMENT_INIT;
    STATEMENT_PREPAREVA("INSERT INTO " QAMPARAMS_TABLE " "
                        "VALUES ("
                        "%d,"
                        "%d," /* QAMPARAM_FREQ */
                        "%d," /* QAMPARAM_INVERSION */
                        "%d," /* QAMPARAM_SYMBOL_RATE */
                        "%d," /* QAMPARAM_FEC_INNER */
                        "%d" /* QAMPARAM_MODULATION */
                        ");",
                        uid,
                        feparams->frequency,
                        feparams->inversion,
                        feparams->u.qam.symbol_rate,
                        feparams->u.qam.fec_inner,
                        feparams->u.qam.modulation);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();

    STATEMENT_FINALIZE();
    return rc;
}

static int VSBParametersGet(int uid, struct dvb_frontend_parameters *feparams)
{
    STATEMENT_INIT;
    STATEMENT_PREPAREVA("SELECT "
                        VSBPARAM_FREQ ","
                        VSBPARAM_MODULATION " "
                        "FROM " VSBPARAMS_TABLE " WHERE " VSBPARAM_MULTIPLEXUID"=%d;"
                        ,uid);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();
    if (rc == SQLITE_ROW)
    {
        feparams->frequency           = STATEMENT_COLUMN_INT(0);
        feparams->inversion           = INVERSION_AUTO;
        feparams->u.vsb.modulation    = STATEMENT_COLUMN_INT(1);
        rc = 0;
    }
    STATEMENT_FINALIZE();
    return rc;
}

static int VSBParametersAdd(int uid, struct dvb_frontend_parameters *feparams)
{
    STATEMENT_INIT;
    STATEMENT_PREPAREVA("INSERT INTO " VSBPARAMS_TABLE " "
                        "VALUES ("
                        "%d,"
                        "%d," /* VSBPARAM_FREQ */
                        "%d" /* VSBPARAM_MODULATION */
                        ");",
                        uid,
                        feparams->frequency,
                        feparams->u.vsb.modulation);
    RETURN_RC_ON_ERROR;

    STATEMENT_STEP();

    STATEMENT_FINALIZE();
    return rc;
}

