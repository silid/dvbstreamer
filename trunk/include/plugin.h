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

#include "config.h"
#include "types.h"
#include "ts.h"
#include "commands.h"
/**
 * @defgroup Plugin Plugin Interface
 * @{
 */

/**
 * Constant for No Feature, use to end a list of features.
 */
#define PLUGIN_FEATURE_TYPE_NONE           0x00
/**
 * Constant for a Filter plugin feature.
 */
#define PLUGIN_FEATURE_TYPE_FILTER         0x01
/**
 * Constant for a PAT processor plugin feature.
 */
#define PLUGIN_FEATURE_TYPE_PATPROCESSOR   0x02
/**
 * Constant for a PMT processor plugin feature.
 */
#define PLUGIN_FEATURE_TYPE_PMTPROCESSOR   0x03
/**
 * Constant for a Delivery Method plugin feature.
 */
#define PLUGIN_FEATURE_TYPE_DELIVERYMETHOD 0x04
/**
 * Constant for a Primary Channel Changed feature.
 */
#define PLUGIN_FEATURE_TYPE_CHANNELCHANGED 0x05

/**
 * Structure used to describe a single 'feature' of a plugin.
 */
typedef struct PluginFeature_t
{
	int type;       /**< Type of this feature. Use PLUGIN_FEATURE_TYPE_NONE to end a list.*/
	void *details;  /**< Pointer to a structure containing specific details for the feature. */
}PluginFeature_t;


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
    unsigned int RequiredVersion; /**< Version of DVBStreamer required to run this plugin */
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
#define PLUGIN_INTERFACE_C(_name, _version, _desc, _author) \
    Plugin_t PluginInterface = { DVBSTREAMER_VERSION, _name, _version, _desc, _author, PluginCommands, NULL}

/**
 * Simple macro to help in defining the Plugin Interface with only features.
 */
#define PLUGIN_INTERFACE_F(_name, _version, _desc, _author) \
    Plugin_t PluginInterface = { DVBSTREAMER_VERSION, _name, _version, _desc, _author, NULL, PluginFeatures}

/**
 * Simple macro to help in defining the Plugin Interface with both commands and
 * features
 */
#define PLUGIN_INTERFACE_CF(_name, _version, _desc, _author) \
    Plugin_t PluginInterface = { DVBSTREAMER_VERSION, _name, _version, _desc, _author, PluginCommands, PluginFeatures}

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
        {NULL, FALSE, 0, 0, NULL, NULL}\
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
        {PLUGIN_FEATURE_TYPE_NONE, NULL}\
    }

/**
 * Simple macro to define a Plugin Filter feature
 * @param _filter A PluginFilter_t instance (will be dereferenced).
 */
#define PLUGIN_FEATURE_FILTER(_filter)          {PLUGIN_FEATURE_TYPE_FILTER, (void*)&_filter}
/**
 * Simple macro to define a PAT Processor feature.
 * @param _processor Function to call when a new PAT arrives.
 */
#define PLUGIN_FEATURE_PATPROCESSOR(_processor) {PLUGIN_FEATURE_TYPE_PATPROCESSOR, (void*)_processor}
/**
 * Simple macro to define a PMT Processor feature.
 * @param _processor Function to call when a new PMT arrives.
 */
#define PLUGIN_FEATURE_PMTPROCESSOR(_processor) {PLUGIN_FEATURE_TYPE_PMTPROCESSOR, (void*)_processor}
/**
 * Simple macro to define a delivery method feature.
 * @param _method A DeliveryMethod_t instance (will be dereferenced).
 */
#define PLUGIN_FEATURE_DELIVERYMETHOD(_method)  {PLUGIN_FEATURE_TYPE_DELIVERYMETHOD, (void*)&_method}

/**
 * Simple macro to define a Channel Changed feature.
 * @param _cchanaged Function to call when the Primary service filter is updated.
 */
#define PLUGIN_FEATURE_CHANNELCHANGED(_cchanged) {PLUGIN_FEATURE_TYPE_CHANNELCHANGED, (void*)_cchanged}

/**
 * Structure used to describe a Filter Feature.
 * Multiple filter features per plugin is allowed, but developers should try and
 * keep the number to a minimum to keep the overheads of maintaining and calling
 * lots of filters down.
 */
typedef struct PluginFilter_t
{
    PIDFilter_t *filter; /**< Filter assigned to this filter feature */
	void (*InitFilter)(PIDFilter_t* filter); /**< Function pointer used to initialise the filter. */
	void (*DeinitFilter)(PIDFilter_t* filter); /**< Function pointer used to deinitialise the filter. */
}PluginFilter_t;



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
 * Function pointer to function to call after the primary service
 * filter is updated. The newMultiplex argument will contain the multiplex currently
 * being used and the newService will contain the service now being filtered.
 * For use with the PLUGIN_FEATURE_TYPE_CHANNELCHANGED feature type, only 1 per
 * plugin is expected (allowed).
 */
typedef void (*PluginChannelChanged_t)(Multiplex_t *newMultiplex, Service_t *newService);

/** @} */
#endif