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

plugin.h

Plugin Interface structures and macros.

*/
#ifndef _PLUGIN_H
#define _PLUGIN_H

/* Annoyingly libdvbpsi causes a preprocessor warning when its header files are included more than once! */
#ifndef _DVBPSI_DVBPSI_H_
#include <dvbpsi/dvbpsi.h>
#endif
#ifndef _DVBPSI_DESCRIPTOR_H_
#include <dvbpsi/descriptor.h>
#endif
#ifndef _DVBPSI_PSI_H_
#include <dvbpsi/psi.h>
#endif
#ifndef _DVBPSI_PAT_H_
#include <dvbpsi/pat.h>
#endif
#ifndef _DVBPSI_PMT_H_
#include <dvbpsi/pmt.h>
#endif
#ifndef _DVBPSI_DEMUX_H_
#include <dvbpsi/demux.h>
#endif
#ifndef _DVBPSI_SDT_H_
#include <dvbpsi/sdt.h>
#endif
#include "dvbpsi/datetime.h"
#include "dvbpsi/nit.h"
#include "dvbpsi/tdttot.h"
#include "dvbpsi/atsc/mgt.h"
#include "dvbpsi/atsc/stt.h"
#include "dvbpsi/atsc/vct.h"

#include "config.h"
#include "types.h"
#include "ts.h"
#include "commands.h"
#include "events.h"
#include "deliverymethod.h"

/**
 * @defgroup Plugin Plugin Interface
 * @{
 */

/**
 * Constant for No Feature, use to end a list of features.
 */
#define PLUGIN_FEATURE_TYPE_NONE             0x00
/**
 * Constant for Event Listener callback.
 */
#define PLUGIN_FEATURE_TYPE_EVENT_LISTENER   0x01
/**
 * Constant for a PAT processor plugin feature.
 */
#define PLUGIN_FEATURE_TYPE_PATPROCESSOR     0x02
/**
 * Constant for a PMT processor plugin feature.
 */
#define PLUGIN_FEATURE_TYPE_PMTPROCESSOR     0x03
/**
 * Constant for a Delivery Method plugin feature.
 */
#define PLUGIN_FEATURE_TYPE_DELIVERYMETHOD   0x04
/**
 * Constant for a SDT processor plugin feature.
 */
#define PLUGIN_FEATURE_TYPE_SDTPROCESSOR     0x05
/**
 * Constant for a NIT processor plugin feature.
 */
#define PLUGIN_FEATURE_TYPE_NITPROCESSOR     0x06
/**
 * Constant for a TDT/TOT processor plugin feature.
 */
#define PLUGIN_FEATURE_TYPE_TDTPROCESSOR     0x07
/**
 * Constant for a MGT processor plugin feature.
 */
#define PLUGIN_FEATURE_TYPE_MGTPROCESSOR     0x08
/**
 * Constant for a STT processor plugin feature.
 */
#define PLUGIN_FEATURE_TYPE_STTPROCESSOR     0x09
/**
 * Constant for a VCT processor plugin feature.
 */
#define PLUGIN_FEATURE_TYPE_VCTPROCESSOR     0x0A
/**
 * Constant for the plugin installed feature.
 */
#define PLUGIN_FEATURE_TYPE_INSTALL          0xFF

typedef struct PluginEventListenerDetails_s
{
    char *name;
    EventListener_t callback;
    void *arg;
}PluginEventListenerDetails_t;

/**
 * Function pointer to function to call when a new PAT arrives.
 * For use with the PLUGIN_FEATURE_TYPE_PATPROCESSOR feature type, only 1 per
 * plugin is expected (allowed).
 */
typedef void (*PluginPATProcessor_t)(dvbpsi_pat_t* newpat);

/**
 * Function pointer to function to call when a new PMT arrives.
 * For use with the PLUGIN_FEATURE_TYPE_PMTPROCESSOR feature type, only 1 per
 * plugin is expected (allowed).
 */
typedef void (*PluginPMTProcessor_t)(dvbpsi_pmt_t* newpmt);

/**
 * Function pointer to function to call when a new SDT arrives.
 * For use with the PLUGIN_FEATURE_TYPE_SDTPROCESSOR feature type, only 1 per
 * plugin is expected (allowed).
 */
typedef void (*PluginSDTProcessor_t)(dvbpsi_sdt_t* newsdt);

/**
 * Function pointer to function to call when a new NIT arrives.
 * For use with the PLUGIN_FEATURE_TYPE_NITPROCESSOR feature type, only 1 per
 * plugin is expected (allowed).
 */
typedef void (*PluginNITProcessor_t)(dvbpsi_nit_t* newnit);

/**
 * Function pointer to function to call when a new TDT/TOT arrives.
 * For use with the PLUGIN_FEATURE_TYPE_TDTPROCESSOR feature type, only 1 per
 * plugin is expected (allowed).
 */
typedef void (*PluginTDTProcessor_t)(dvbpsi_tdt_tot_t* newtdttot);

/**
 * Function pointer to function to call when a new MGT arrives.
 * For use with the PLUGIN_FEATURE_TYPE_MGTPROCESSOR feature type, only 1 per
 * plugin is expected (allowed).
 */
typedef void (*PluginMGTProcessor_t)(dvbpsi_atsc_mgt_t* newmgt);

/**
 * Function pointer to function to call when a new STT arrives.
 * For use with the PLUGIN_FEATURE_TYPE_STTPROCESSOR feature type, only 1 per
 * plugin is expected (allowed).
 */
typedef void (*PluginSTTProcessor_t)(dvbpsi_atsc_stt_t* newstt);

/**
 * Function pointer to function to call when a new VCT arrives.
 * For use with the PLUGIN_FEATURE_TYPE_VCTPROCESSOR feature type, only 1 per
 * plugin is expected (allowed).
 */
typedef void (*PluginVCTProcessor_t)(dvbpsi_atsc_vct_t* newvct);

/**
 * Function pointer to function to call after the primary service
 * filter is updated. The newMultiplex argument will contain the multiplex currently
 * being used and the newService will contain the service now being filtered.
 * For use with the PLUGIN_FEATURE_TYPE_CHANNELCHANGED feature type, only 1 per
 * plugin is expected (allowed).
 */
typedef void (*PluginChannelChanged_t)(Multiplex_t *newMultiplex, Service_t *newService);

/**
 * Function pointer to a function to call when the plugin is (un)installed.
 * installed is TRUE when the plugin is installed and FALSE when being uninstalled.
 */
typedef void (*PluginInstallCallback_t)(bool installed);

/**
 * Structure used to describe a single 'feature' of a plugin.
 */
typedef struct PluginFeature_t
{
    int type;       /**< Type of this feature. Use PLUGIN_FEATURE_TYPE_NONE to end a list.*/
    union{
        PluginEventListenerDetails_t eventDetails;
        DeliveryMethodHandler_t dmHandler;
        PluginPMTProcessor_t    patCB;
        PluginPMTProcessor_t    pmtCB;
        PluginSDTProcessor_t    sdtCB;
        PluginNITProcessor_t    nitCB;
        PluginTDTProcessor_t    tdtCB;
        PluginMGTProcessor_t    mgtCB;
        PluginSTTProcessor_t    sttCB;
        PluginVCTProcessor_t    vctCB;
        PluginChannelChanged_t  channelChangeCB;
        PluginInstallCallback_t installCB;
    }details;
}PluginFeature_t;

/**
 * Constant for a plugin that is just for use when processing DVB signals.
 */
#define PLUGIN_FOR_DVB  0x01
/**
 * Constant for a plugin this is just for use when processing ATSC signals.
 */
#define PLUGIN_FOR_ATSC 0x02
/**
 * Constant for a plugin that should be loaded for any type of signal.
 */
#define PLUGIN_FOR_ALL  0xff

/**
 * Structure used to define a plugin.
 * To create a plugin the shared object should have a global variable of this
 * type called PluginInterface.
 * @code
 *
 * Command_t myCommands[] = {...,
 *                              {NULL, FALSE, 0, 0, NULL,NULL}
 *                          };
 *
 * PluginFeature_t myFeatures[]  = {...,
 *                                      {PLUGIN_FEATURE_TYPE_NONE, NULL}
 *                                 };
 *
 * Plugin_t PluginInterface = {
 *  DVBSTREAMER_VERSION,
 *  "Example",
 *  "0.1",
 *  "Example Plugin",
 *  "An Author",
 *   myCommands,
 *   myFeatures
 * };
 * @endcode
 */
typedef struct Plugin_t
{
    unsigned int requiredVersion; /**< Version of DVBStreamer required to run this plugin */
    unsigned int pluginFor;       /**< What type of transport stream this plugin is meant for. */
	char *name;                   /**< Name of the plugin */
	char *version;                /**< String describing the version of the plugin */
	char *description;            /**< Description of the plugin */
	char *author;                 /**< Author/Contact address for bugs */
	Command_t *commands;          /**< NULL terminated array of commands or NULL for no commands */
	PluginFeature_t *features;    /**< A PLUGIN_FEATURE_NONE terminated list of features or NULL for no features. */
}Plugin_t;

/**
 * Simple macro to help in defining the Plugin Interface with only commands.
 */
#define PLUGIN_INTERFACE_C(_for, _name, _version, _desc, _author) \
    Plugin_t PluginInterface = { DVBSTREAMER_VERSION, _for, _name, _version, _desc, _author, PluginCommands, NULL}

/**
 * Simple macro to help in defining the Plugin Interface with only features.
 */
#define PLUGIN_INTERFACE_F(_for, _name, _version, _desc, _author) \
    Plugin_t PluginInterface = { DVBSTREAMER_VERSION, _for, _name, _version, _desc, _author, NULL, PluginFeatures}

/**
 * Simple macro to help in defining the Plugin Interface with both commands and
 * features
 */
#define PLUGIN_INTERFACE_CF(_for, _name, _version, _desc, _author) \
    Plugin_t PluginInterface = { DVBSTREAMER_VERSION, _for, _name, _version, _desc, _author, PluginCommands, PluginFeatures}

/**
 * Use this macro to define the commands a plugin provides.
 * This should be used before PLUGININTERFACE.
 * for example:
 * @code
 * PLUGIN_COMMANDS({"example", TRUE, 1, 1,"An example command", "example <arg>\nPrint out <arg>",ExampleCommand});
 * PLUGININTERFACE("Example", "0.1", "An example plugin", "A Coder");
 * @endcode
 */
#define PLUGIN_COMMANDS(_commands...) \
    static Command_t PluginCommands[] = {\
        _commands,\
        COMMANDS_SENTINEL\
    }

/**
 * Use this macro to define the features a plugin provides.
 * This should be used before PLUGININTERFACE.
 * for example:
 * @code
 * PLUGIN_FEATURES(
 *  PLUGIN_FEATURE_FILTER(myfilterfeature)
 * );
 * PLUGININTERFACE("Example", "0.1", "An example plugin", "A Coder");
 * @endcode
 * @param _features A list of features provided by this plugin.
 */
#define PLUGIN_FEATURES(_features...) \
    static PluginFeature_t PluginFeatures[] = {\
        _features,\
        {PLUGIN_FEATURE_TYPE_NONE, .details.installCB=NULL}\
    }

/**
 * Simple macro to define an event listener feature.
 * @param _details A PluginEventListenerDetails_t containing the event name and 
 *                 the callback to call when the event occurs.
 */
#define PLUGIN_FEATURE_EVENT_LISTENER(_event, _listener, _arg)\
    {.type=PLUGIN_FEATURE_TYPE_EVENT_LISTENER, .details.eventDetails={_event, _listener, _arg}}
/**
 * Simple macro to define a PAT Processor feature.
 * @param _processor Function to call when a new PAT arrives.
 */
#define PLUGIN_FEATURE_PATPROCESSOR(_processor)\
    {.type=PLUGIN_FEATURE_TYPE_PATPROCESSOR, .details.patCB=_processor}
/**
 * Simple macro to define a PMT Processor feature.
 * @param _processor Function to call when a new PMT arrives.
 */
#define PLUGIN_FEATURE_PMTPROCESSOR(_processor)\
    {.type=PLUGIN_FEATURE_TYPE_PMTPROCESSOR, .details.pmtCB=_processor}
/**
 * Simple macro to define a delivery method feature.
 * @param _method A DeliveryMethod_t instance (will be dereferenced).
 */
#define PLUGIN_FEATURE_DELIVERYMETHOD(_canHandle, _newInstance)\
    {.type=PLUGIN_FEATURE_TYPE_DELIVERYMETHOD, .details.dmHandler={_canHandle,_newInstance}}

/**
 * Simple macro to define a Channel Changed feature.
 * @param _cchanged Function to call when the Primary service filter is updated.
 */
#define PLUGIN_FEATURE_CHANNELCHANGED(_cchanged)\
    {.type=PLUGIN_FEATURE_TYPE_CHANNELCHANGED, .details.channelChangeCB =_cchanged}

/**
 * Simple macro to define a SDT Processor feature.
 * @param _processor Function to call when a new SDT arrives.
 */
#define PLUGIN_FEATURE_SDTPROCESSOR(_processor)\
    {.type=PLUGIN_FEATURE_TYPE_SDTPROCESSOR, .details.sdtCB=_processor}
/**
 * Simple macro to define a NIT Processor feature.
 * @param _processor Function to call when a new NIT arrives.
 */
#define PLUGIN_FEATURE_NITPROCESSOR(_processor)\
    {.type=PLUGIN_FEATURE_TYPE_NITPROCESSOR, .details.nitCB=_processor}
/**
 * Simple macro to define a TDT/TOT Processor feature.
 * @param _processor Function to call when a new TDT or TOT arrives.
 */
#define PLUGIN_FEATURE_TDTPROCESSOR(_processor)\
    {.type=PLUGIN_FEATURE_TYPE_TDTPROCESSOR, .details.tdtCB=_processor}
/**
 * Simple macro to define a MGT Processor feature.
 * @param _processor Function to call when a new MGT arrives.
 */
#define PLUGIN_FEATURE_MGTPROCESSOR(_processor)\
    {.type=PLUGIN_FEATURE_TYPE_MGTPROCESSOR, .details.mgtCB=_processor}

/**
 * Simple macro to define a STT Processor feature.
 * @param _processor Function to call when a new STT arrives.
 */
#define PLUGIN_FEATURE_STTPROCESSOR(_processor)\
    {.type=PLUGIN_FEATURE_TYPE_STTPROCESSOR, .details.sttCB=_processor}

/**
 * Simple macro to define a VCT Processor feature.
 * @param _processor Function to call when a new VCT arrives.
 */
#define PLUGIN_FEATURE_VCTPROCESSOR(_processor)\
    {.type=PLUGIN_FEATURE_TYPE_VCTPROCESSOR, .details.vctCB=_processor}

/**
 * Simple macro to define an install callback.
 * @param _callback A PluginInstallCallback_t to call when the plugin is (un)installed.
 */
#define PLUGIN_FEATURE_INSTALL(_callback)\
    {.type=PLUGIN_FEATURE_TYPE_INSTALL, .details.installCB=_callback}


/** @} */
#endif
