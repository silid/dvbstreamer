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
#include <dvbpsi/dvbpsi.h>
#include <dvbpsi/descriptor.h>
#include <dvbpsi/psi.h>
#include <dvbpsi/pat.h>
#include <dvbpsi/pmt.h>

#include "config.h"
#include "types.h"
#include "comannds.h"
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
 * Structure used to describe a Filter Feature.
 */
typedef struct PluginFilterHandler_t
{
	void (*InitFilter)(PIDFilter_t* filter); /**< Function pointer used to initialise the filter. */
}PluginFilterHandler_t;

/**
 * Structure used to describe a PAT Processor Feature.
 * Implementors should consider the following structure as the 'base class' and
 * should extend it with the state they require.
 * For example:
 * @code
 * struct NITPATProcessor {
 * void (*ProcessPAT)(PluginPATProcessor_t *this, dvbpsi_pat_t* newpat);
 * uint16_t nitpid;
 * };
 * @endcode
 */
typedef struct PluginPATProcessor_t
{
    void (*ProcessPAT)(PluginPATProcessor_t *this, dvbpsi_pat_t* newpat); /**< Function pointer to function to call when a new PAT arrives.*/
}PluginPATProcessor_t;

/**
 * Structure used to describe a PMT Processor Feature.
 * Implementors should consider the following structure as the 'base class' and
 * should extend it with the state they require.
 * For example:
 * @code
 * struct DSMCCPMTProcessor_t {
 * void (*ProcessPMT)(PluginPMTProcessor_t *this, dvbpsi_pmt_t* newpmt);
 * char *carouselBuffer;
 * };
 * @endcode
 */
typedef struct PluginPMTProcessor_t
{
    void (*ProcessPMT)(PluginPMTProcessor_t *this, dvbpsi_pmt_t* newpmt); /**< Function pointer to function to call when a new PMT arrives.*/
}PluginPMTProcessor_t;

/** @} */
#endif
