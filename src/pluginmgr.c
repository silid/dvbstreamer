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

pluginmgr.c

Plugin Manager functions.

*/
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "ltdl.h"
#include "list.h"
#include "deliverymethod.h"
#include "patprocessor.h"
#include "pmtprocessor.h"
#include "sdtprocessor.h"
#include "nitprocessor.h"
#include "tdtprocessor.h"
#include "sectionprocessor.h"
#include "psipprocessor.h"
#include "pesprocessor.h"
#include "pluginmgr.h"
#include "plugin.h"
#include "logging.h"
#include "main.h"
#include "tuning.h"

/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/
#define PLUGIN_FEATURE_INFO(_feature) {_feature, # _feature}
/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/

struct PluginEntry_t
{
    lt_dlhandle handle;
    Plugin_t *pluginInterface;
};

struct PluginFeatureInfo_t
{
    int feature;
    char *name;
};

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static int PluginManagerLoadPlugin(const char *filename, void *userarg);
static void PluginManagerUnloadPlugin(struct PluginEntry_t *entry);
static void PluginManagerInstallPlugin(Plugin_t *pluginInterface);
static void PluginManagerUninstallPlugin(Plugin_t *pluginInterface);

static void PluginManagerLsPlugins(int argc, char **argv);
static void PluginManagerPluginInfo(int argc, char **argv);

static char *FindPluginFeatureName(int type);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static List_t *PluginsList;

static Command_t PluginManagerCommands[] = {
        {
            "lsplugins",
            FALSE, 0, 0,
            "List loaded plugins.",
            "List all plugins that where loaded at startup.",
            PluginManagerLsPlugins
        },
        {
            "plugininfo",
            TRUE, 1, 1,
            "Display the information about a plugin.",
            "plugininfo <pluginname>\n"
            "Displays the version, author and descriptor for a specific plugin.",
            PluginManagerPluginInfo
        },
        {NULL, FALSE, 0, 0, NULL, NULL}
    };

struct PluginFeatureInfo_t pluginFeatures[] = {
    PLUGIN_FEATURE_INFO(PLUGIN_FEATURE_TYPE_FILTER),
    PLUGIN_FEATURE_INFO(PLUGIN_FEATURE_TYPE_PATPROCESSOR),
    PLUGIN_FEATURE_INFO(PLUGIN_FEATURE_TYPE_PMTPROCESSOR),
    PLUGIN_FEATURE_INFO(PLUGIN_FEATURE_TYPE_DELIVERYMETHOD),
    PLUGIN_FEATURE_INFO(PLUGIN_FEATURE_TYPE_CHANNELCHANGED),
    PLUGIN_FEATURE_INFO(PLUGIN_FEATURE_TYPE_SDTPROCESSOR),
    PLUGIN_FEATURE_INFO(PLUGIN_FEATURE_TYPE_NITPROCESSOR),
    PLUGIN_FEATURE_INFO(PLUGIN_FEATURE_TYPE_TDTPROCESSOR),
    PLUGIN_FEATURE_INFO(PLUGIN_FEATURE_TYPE_SECTIONPROCESSOR),
    PLUGIN_FEATURE_INFO(PLUGIN_FEATURE_TYPE_PESPROCESSOR),
    PLUGIN_FEATURE_INFO(PLUGIN_FEATURE_TYPE_INSTALL),
    PLUGIN_FEATURE_INFO(PLUGIN_FEATURE_TYPE_MGTPROCESSOR),
    PLUGIN_FEATURE_INFO(PLUGIN_FEATURE_TYPE_STTPROCESSOR),
    PLUGIN_FEATURE_INFO(PLUGIN_FEATURE_TYPE_VCTPROCESSOR),
    PLUGIN_FEATURE_INFO(PLUGIN_FEATURE_TYPE_NONE)

};

static char PLUGINMANAGER[] = "PluginManager";

#ifdef __CYGWIN__    

#if defined(ENABLE_DVB)
extern Plugin_t LCNQueryPluginInterface;
extern Plugin_t NowNextPluginInterface;
extern Plugin_t DVBSchedulePluginInterface;
#endif

extern Plugin_t EPGtoXMLTVPluginInterface;
extern Plugin_t UDPOutputPluginInterface;

#if defined(ENABLE_ATSC)
extern Plugin_t ATSCtoEPGPluginInterface;
#endif

#endif
/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
int PluginManagerInit(void)
{
    ListIterator_t iterator;
    uint8_t isSuitableMask;
    lt_dlinit();
    PluginsList = ListCreate();
    LogModule(LOG_DEBUG, PLUGINMANAGER, "Plugin Manager Initialising...\n");
    lt_dlsetsearchpath(DVBSTREAMER_PLUGINDIR);

    /* Load all the plugins */
    lt_dlforeachfile(DVBSTREAMER_PLUGINDIR, PluginManagerLoadPlugin, NULL);
#ifdef __CYGWIN__
    {
        struct PluginEntry_t *entry;
#if defined(ENABLE_DVB)
        entry = calloc(1, sizeof(struct PluginEntry_t));
        entry->pluginInterface = &LCNQueryPluginInterface;
        ListAdd(PluginsList, entry);
        entry = calloc(1, sizeof(struct PluginEntry_t));
        entry->pluginInterface = &NowNextPluginInterface;
        ListAdd(PluginsList, entry);
        entry = calloc(1, sizeof(struct PluginEntry_t));
        entry->pluginInterface = &DVBSchedulePluginInterface;
        ListAdd(PluginsList, entry);        
#endif

        entry = calloc(1, sizeof(struct PluginEntry_t));
        entry->pluginInterface = &EPGtoXMLTVPluginInterface;
        ListAdd(PluginsList, entry);                

        entry = calloc(1, sizeof(struct PluginEntry_t));
        entry->pluginInterface = &UDPOutputPluginInterface;
        ListAdd(PluginsList, entry);                
        
#if defined(ENABLE_ATSC)
        entry = calloc(1, sizeof(struct PluginEntry_t));
        entry->pluginInterface = &ATSCtoEPGPluginInterface;
        ListAdd(PluginsList, entry);                
#endif        
    }
#endif
    isSuitableMask = MainIsDVB() ? PLUGIN_FOR_DVB:PLUGIN_FOR_ATSC;

    /* Process the plugins */
    for ( ListIterator_Init(iterator, PluginsList); ListIterator_MoreEntries(iterator);)
    {
        struct PluginEntry_t *entry = ListIterator_Current(iterator);
        if ((entry->pluginInterface->pluginFor & isSuitableMask) != 0)
        {
            LogModule(LOG_DEBUG, PLUGINMANAGER,"Installing %s\n", entry->pluginInterface->name);
            PluginManagerInstallPlugin(entry->pluginInterface);
            ListIterator_Next(iterator);
        }
        else
        {
            LogModule(LOG_DEBUG, PLUGINMANAGER, "Not installing %s as not suitable.\n", entry->pluginInterface->name);
            lt_dlclose(entry->handle);
            free(entry);
            ListRemoveCurrent(&iterator); /* Removing an entry automatically moves to the next entry in the list */
        }
    }

    CommandRegisterCommands(PluginManagerCommands);
    LogModule(LOG_DEBUG, PLUGINMANAGER, "Plugin Manager Initialised\n");
    return 0;
}

void PluginManagerDeInit(void)
{
    LogModule(LOG_DEBUG, PLUGINMANAGER,"Plugin Manager Deinitialising...\n");
    CommandUnRegisterCommands(PluginManagerCommands);
    ListFree(PluginsList, (ListDataDestructor_t)PluginManagerUnloadPlugin);
    lt_dlexit();
    LogModule(LOG_DEBUG, PLUGINMANAGER,"Plugin Manager Deinitialised\n");
}

/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/
static int PluginManagerLoadPlugin(const char *filename, void *userarg)
{
    lt_dlhandle handle = lt_dlopenext(filename);
    LogModule(LOG_DEBUGV, PLUGINMANAGER,"Attempting to load %s\n", filename);
    if (handle)
    {
        Plugin_t *pluginInterface = lt_dlsym(handle, "PluginInterface");

        if (pluginInterface)
        {
            ListIterator_t iterator;
            struct PluginEntry_t *entry = NULL;
            bool addPlugin = TRUE;
            if (pluginInterface->requiredVersion !=  DVBSTREAMER_VERSION)
            {
                addPlugin = FALSE;
            }

            if (addPlugin)
            {
                /*
                 * Only add the plugin if this is a unique plugin, ie no plugin
                 * with the same name has already been loaded
                 */
                for ( ListIterator_Init(iterator, PluginsList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
                {
                    struct PluginEntry_t *entry = ListIterator_Current(iterator);
                    if (strcmp(pluginInterface->name, entry->pluginInterface->name) == 0)
                    {
                        LogModule(LOG_DEBUGV, PLUGINMANAGER,"Plugin already loaded, igoring this instance.\n");
                        addPlugin = FALSE;
                        break;
                    }
                }
            }

            if (addPlugin)
            {
                entry = calloc(1, sizeof(struct PluginEntry_t));
                if (entry)
                {
                    entry->pluginInterface = pluginInterface;
                    entry->handle = handle;
                    if (ListAdd(PluginsList, entry))
                    {
                        LogModule(LOG_INFOV, PLUGINMANAGER, "Loaded plugin %s\n", pluginInterface->name);
                        return 0;
                    }
                    free(entry);
                }
            }
        }
        else
        {
            LogModule(LOG_DEBUGV, PLUGINMANAGER,"PluginInterface not found for %s.\n", filename);
        }
        lt_dlclose(handle);
    }
    else
    {
        LogModule(LOG_DEBUGV, PLUGINMANAGER,"Failed to open plugin %s - reason %s\n", filename, lt_dlerror());
    }
    return 0;
}

static void PluginManagerUnloadPlugin(struct PluginEntry_t *entry)
{
    LogModule(LOG_DEBUG, PLUGINMANAGER, "Uninstalling %s\n", entry->pluginInterface->name);
    PluginManagerUninstallPlugin(entry->pluginInterface);
    if (entry->handle)
    {
        lt_dlclose(entry->handle);
    }
    free(entry);
}

static void PluginManagerInstallPlugin(Plugin_t *pluginInterface)
{
    int i;
    if (pluginInterface->commands)
    {
        CommandRegisterCommands(pluginInterface->commands);
    }
    if (pluginInterface->features)
    {
        for (i = 0; pluginInterface->features[i].type != PLUGIN_FEATURE_TYPE_NONE; i++)
        {
            switch(pluginInterface->features[i].type)
            {
                case PLUGIN_FEATURE_TYPE_FILTER:
                    {
                        PluginFilter_t *pluginFilter = pluginInterface->features[i].details;
                        pluginFilter->filter = PIDFilterAllocate(MainTSFilterGet());
                        if (pluginFilter->filter)
                        {
                            LogModule(LOG_DEBUGV, PLUGINMANAGER,"plugin %s: Installed filter.\n", pluginInterface->name);
                            pluginFilter->InitFilter(pluginFilter->filter);
                        }
                    }
                    break;
                case PLUGIN_FEATURE_TYPE_PATPROCESSOR:
                    LogModule(LOG_DEBUGV, PLUGINMANAGER,"plugin %s: Installed PAT processor.\n", pluginInterface->name);
                    PATProcessorRegisterPATCallback(pluginInterface->features[i].details);
                    break;
                case PLUGIN_FEATURE_TYPE_PMTPROCESSOR:
                    LogModule(LOG_DEBUGV, PLUGINMANAGER,"plugin %s: Installed PMT processor.\n", pluginInterface->name);
                    PMTProcessorRegisterPMTCallback(pluginInterface->features[i].details);
                    break;
                case PLUGIN_FEATURE_TYPE_DELIVERYMETHOD:
                    LogModule(LOG_DEBUGV, PLUGINMANAGER,"plugin %s: Installed Delivery method.\n", pluginInterface->name);
                    DeliveryMethodManagerRegister(pluginInterface->features[i].details);
                    break;
                case PLUGIN_FEATURE_TYPE_CHANNELCHANGED:
                    LogModule(LOG_DEBUGV, PLUGINMANAGER,"plugin %s: Installed channel changed callback.\n", pluginInterface->name);
                    TuningChannelChangedRegisterCallback(pluginInterface->features[i].details);
                    break;
#if defined(ENABLE_DVB)                    
                case PLUGIN_FEATURE_TYPE_SDTPROCESSOR:
                    LogModule(LOG_DEBUGV, PLUGINMANAGER,"plugin %s: Installed SDT processor.\n", pluginInterface->name);
                    SDTProcessorRegisterSDTCallback(pluginInterface->features[i].details);
                    break;
                case PLUGIN_FEATURE_TYPE_NITPROCESSOR:
                    LogModule(LOG_DEBUGV, PLUGINMANAGER,"plugin %s: Installed NIT processor.\n", pluginInterface->name);
                    NITProcessorRegisterNITCallback(pluginInterface->features[i].details);
                    break;
                case PLUGIN_FEATURE_TYPE_TDTPROCESSOR:
                    LogModule(LOG_DEBUGV, PLUGINMANAGER,"plugin %s: Installed TDT processor.\n", pluginInterface->name);
                    TDTProcessorRegisterTDTCallback(pluginInterface->features[i].details);
                    break;
#endif                    
                case PLUGIN_FEATURE_TYPE_SECTIONPROCESSOR:
                    {
                        PluginSectionProcessorDetails_t *details = pluginInterface->features[i].details;
                        LogModule(LOG_DEBUGV, PLUGINMANAGER,"plugin %s: Installed section processor.\n", pluginInterface->name);
                        SectionProcessorStartPID(details->pid, details->processor, details->userarg);
                    }
                    break;
                case PLUGIN_FEATURE_TYPE_PESPROCESSOR:
                    {
                        PluginPESProcessorDetails_t *details = pluginInterface->features[i].details;
                        LogModule(LOG_DEBUGV, PLUGINMANAGER,"plugin %s: Installed PES processor.\n", pluginInterface->name);
                        PESProcessorStartPID(details->pid, details->processor, details->userarg);
                    }
                    break;
#if defined(ENABLE_ATSC)                    
                case PLUGIN_FEATURE_TYPE_MGTPROCESSOR:
                    LogModule(LOG_DEBUGV, PLUGINMANAGER,"plugin %s: Installed MGT processor.\n", pluginInterface->name);
                    PSIPProcessorRegisterMGTCallback(pluginInterface->features[i].details);
                    break;                    
                case PLUGIN_FEATURE_TYPE_STTPROCESSOR:
                    LogModule(LOG_DEBUGV, PLUGINMANAGER,"plugin %s: Installed STT processor.\n", pluginInterface->name);
                    PSIPProcessorRegisterSTTCallback(pluginInterface->features[i].details);
                    break;                    
                case PLUGIN_FEATURE_TYPE_VCTPROCESSOR:
                    LogModule(LOG_DEBUGV, PLUGINMANAGER,"plugin %s: Installed VCT processor.\n", pluginInterface->name);
                    PSIPProcessorRegisterVCTCallback(pluginInterface->features[i].details);
                    break;                    
#endif
                case PLUGIN_FEATURE_TYPE_INSTALL:
                    {
                        PluginInstallCallback_t callback = pluginInterface->features[i].details;
                        callback(TRUE);
                    }
                    break;                
            }

        }
    }
}

static void PluginManagerUninstallPlugin(Plugin_t *pluginInterface)
{
    int i;
    if (pluginInterface->commands)
    {
        CommandUnRegisterCommands(pluginInterface->commands);
    }
    if (pluginInterface->features)
    {
        for (i = 0; pluginInterface->features[i].type != PLUGIN_FEATURE_TYPE_NONE; i++)
        {
            switch(pluginInterface->features[i].type)
            {
                case PLUGIN_FEATURE_TYPE_FILTER:
                    {
                        PluginFilter_t *pluginFilter = pluginInterface->features[i].details;
                        if (pluginFilter->filter)
                        {
                            LogModule(LOG_DEBUGV, PLUGINMANAGER,"plugin %s: Uninstalled filter.\n", pluginInterface->name);
                            pluginFilter->DeinitFilter(pluginFilter->filter);
                            PIDFilterFree(pluginFilter->filter);
                        }
                    }
                    break;
                case PLUGIN_FEATURE_TYPE_PATPROCESSOR:
                    LogModule(LOG_DEBUGV, PLUGINMANAGER,"plugin %s: Uninstalled PAT processor.\n", pluginInterface->name);
                    PATProcessorUnRegisterPATCallback(pluginInterface->features[i].details);
                    break;
                case PLUGIN_FEATURE_TYPE_PMTPROCESSOR:
                    LogModule(LOG_DEBUGV, PLUGINMANAGER,"plugin %s: Uninstalled PMT processor.\n", pluginInterface->name);
                    PMTProcessorUnRegisterPMTCallback(pluginInterface->features[i].details);
                    break;
                case PLUGIN_FEATURE_TYPE_DELIVERYMETHOD:
                    LogModule(LOG_DEBUGV, PLUGINMANAGER,"plugin %s: Uninstalled Delivery method.\n", pluginInterface->name);
                    DeliveryMethodManagerUnRegister(pluginInterface->features[i].details);
                    break;
                case PLUGIN_FEATURE_TYPE_CHANNELCHANGED:
                    LogModule(LOG_DEBUGV, PLUGINMANAGER,"plugin %s: Uninstalled channel changed callback.\n", pluginInterface->name);
                    TuningChannelChangedUnRegisterCallback(pluginInterface->features[i].details);
                    break;
#if defined(ENABLE_DVB)                    
                case PLUGIN_FEATURE_TYPE_SDTPROCESSOR:
                    LogModule(LOG_DEBUGV, PLUGINMANAGER,"plugin %s: Uninstalled SDT processor.\n", pluginInterface->name);
                    SDTProcessorUnRegisterSDTCallback(pluginInterface->features[i].details);
                    break;
                case PLUGIN_FEATURE_TYPE_NITPROCESSOR:
                    LogModule(LOG_DEBUGV, PLUGINMANAGER,"plugin %s: Uninstalled NIT processor.\n", pluginInterface->name);
                    NITProcessorUnRegisterNITCallback(pluginInterface->features[i].details);
                    break;
                case PLUGIN_FEATURE_TYPE_TDTPROCESSOR:
                    LogModule(LOG_DEBUGV, PLUGINMANAGER,"plugin %s: Uninstalled TDT processor.\n", pluginInterface->name);
                    TDTProcessorUnRegisterTDTCallback(pluginInterface->features[i].details);
                    break;
#endif                    
                case PLUGIN_FEATURE_TYPE_SECTIONPROCESSOR:
                    {
                        PluginSectionProcessorDetails_t *details = pluginInterface->features[i].details;
                        LogModule(LOG_DEBUGV, PLUGINMANAGER,"plugin %s: Uninstalled section processor.\n", pluginInterface->name);
                        SectionProcessorStopPID(details->pid, details->processor, details->userarg);
                    }
                    break;
                case PLUGIN_FEATURE_TYPE_PESPROCESSOR:
                    {
                        PluginPESProcessorDetails_t *details = pluginInterface->features[i].details;
                        LogModule(LOG_DEBUGV, PLUGINMANAGER,"plugin %s: Uninstalled PES processor.\n", pluginInterface->name);
                        PESProcessorStopPID(details->pid, details->processor, details->userarg);
                    }
                    break;
#if defined(ENABLE_ATSC)                    
                case PLUGIN_FEATURE_TYPE_MGTPROCESSOR:
                    LogModule(LOG_DEBUGV, PLUGINMANAGER,"plugin %s: Uninstalled MGT processor.\n", pluginInterface->name);
                    PSIPProcessorUnRegisterMGTCallback(pluginInterface->features[i].details);
                    break;
                case PLUGIN_FEATURE_TYPE_STTPROCESSOR:
                    LogModule(LOG_DEBUGV, PLUGINMANAGER,"plugin %s: Uninstalled STT processor.\n", pluginInterface->name);
                    PSIPProcessorUnRegisterSTTCallback(pluginInterface->features[i].details);
                    break;
                case PLUGIN_FEATURE_TYPE_VCTPROCESSOR:
                    LogModule(LOG_DEBUGV, PLUGINMANAGER,"plugin %s: Uninstalled VCT processor.\n", pluginInterface->name);
                    PSIPProcessorUnRegisterVCTCallback(pluginInterface->features[i].details);
                    break;                    
#endif
                case PLUGIN_FEATURE_TYPE_INSTALL:
                    {
                        PluginInstallCallback_t callback = pluginInterface->features[i].details;
                        callback(FALSE);
                    }
                    break;
            }
        }
    }
}

static void PluginManagerLsPlugins(int argc, char **argv)
{
    ListIterator_t iterator;
    for ( ListIterator_Init(iterator, PluginsList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        struct PluginEntry_t *entry = ListIterator_Current(iterator);
        CommandPrintf("%s\n", entry->pluginInterface->name);
    }
}

static void PluginManagerPluginInfo(int argc, char **argv)
{
    ListIterator_t iterator;
    Plugin_t *pluginInterface = NULL;
    for ( ListIterator_Init(iterator, PluginsList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        struct PluginEntry_t *entry = ListIterator_Current(iterator);

        if (strcmp(entry->pluginInterface->name, argv[0]) == 0)
        {
            pluginInterface = entry->pluginInterface;
            break;
        }
    }

    if (pluginInterface)
    {
        int i;
        char *pluginFor = "<Invalid>";
        
        CommandPrintf("Name        : %s\n"
                      "Version     : %s\n"
                      "Author      : %s\n"
                      "Description :\n%s\n\n",
                      pluginInterface->name,
                      pluginInterface->version,
                      pluginInterface->author,
                      pluginInterface->description);

        CommandPrintf("Plugin Details\n"
                      "--------------\n");
        switch(pluginInterface->pluginFor)
        {
            case PLUGIN_FOR_DVB:
                pluginFor = "DVB";
                break;
            case PLUGIN_FOR_ATSC:
                pluginFor = "ATSC";
                break;
            case PLUGIN_FOR_ALL:
                pluginFor = "All transport types";
                break;
        }
        CommandPrintf("\nPlugin For : %s\n", pluginFor);
        
        CommandPrintf("\nFeatures   :\n");
        if (pluginInterface->features)
        {
            for (i = 0;(pluginInterface->features[i].type != PLUGIN_FEATURE_TYPE_NONE); i++)
            {
                char *name = FindPluginFeatureName(pluginInterface->features[i].type);
                if (name)
                {
                    CommandPrintf("\t%s\n", name);
                }
                else
                {
                    CommandPrintf("\t<Invalid Feature type %d>\n", pluginInterface->features[i].type);
                }
            }
        }
        else
        {
            CommandPrintf("\t<None>\n");
        }
        CommandPrintf("\nCommands   :\n");
        if (pluginInterface->commands)
        {
            for (i = 0;pluginInterface->commands[i].command; i ++)
            {
                CommandPrintf("\t%s\n", pluginInterface->commands[i].command);
            }
        }
         else
        {
            CommandPrintf("\t<None>\n");
        }
        CommandPrintf("\n");
    }
    else
    {
        CommandPrintf("Plugin \"%s\" not found.\n", argv[0]);
    }
}

/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/

static char *FindPluginFeatureName(int type)
{
    int i;
    for (i = 0; pluginFeatures[i].feature != PLUGIN_FEATURE_TYPE_NONE; i ++)
    {
        if (pluginFeatures[i].feature == type)
        {
            return pluginFeatures[i].name;
        }
    }
    return NULL;
}
