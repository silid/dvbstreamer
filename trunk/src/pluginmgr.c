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

#include "config.h"
#include "ltdl.h"
#include "list.h"
#include "deliverymethod.h"
#include "pluginmgr.h"
#include "plugin.h"
#include "logging.h"

static int PluginLoadPlugin(const char *filename, void *userarg);
static void PluginInstallPlugin(Plugin_t *pluginInterface);
static void PluginUninstallPlugin(Plugin_t *pluginInterface);

struct PluginEntry_t
{
    lt_dlhandle handle;
    Plugin_t *pluginInterface;
};

List_t *PluginsList;


int PluginManagerInit(void)
{
    ListIterator_t iterator;
    lt_dlinit();
    PluginsList = ListCreate();
    printlog(LOG_DEBUG, "Plugin Manager Initialising...\n");
    lt_dlsetsearchpath(DVBSTREAMER_PLUGINDIR);

    /* Load all the plugins */
    lt_dlforeachfile(DVBSTREAMER_PLUGINDIR, PluginLoadPlugin, NULL);

    /* Process the plugins */
    for ( ListIterator_Init(iterator, PluginsList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        struct PluginEntry_t *entry = ListIterator_Current(iterator);
        printlog(LOG_DEBUG, "Installing %s\n", entry->pluginInterface->name);
        PluginInstallPlugin(entry->pluginInterface);
    }
    printlog(LOG_DEBUG, "Plugin Manager Initialised\n");
    return 0;
}

void PluginManagerDeInit(void)
{
    ListIterator_t iterator;
    printlog(LOG_DEBUG, "Plugin Manager Deinitialising...\n");
    for ( ListIterator_Init(iterator, PluginsList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        struct PluginEntry_t *entry = ListIterator_Current(iterator);
        printlog(LOG_DEBUG, "Uninstalling %s\n", entry->pluginInterface->name);
        PluginUninstallPlugin(entry->pluginInterface);
        ListRemoveCurrent(&iterator);
        lt_dlclose(entry->handle);
        free(entry);
    }
    ListFree(PluginsList);
    lt_dlexit();
    printlog(LOG_DEBUG, "Plugin Manager Deinitialised\n");
}

static int PluginLoadPlugin(const char *filename, void *userarg)
{
    lt_dlhandle handle = lt_dlopenext(filename);
    printlog(LOG_INFOV, "Attempting to load %s\n", filename);
    if (handle)
    {
        Plugin_t *pluginInterface = lt_dlsym(handle, "PluginInterface");

        if (pluginInterface)
        {
            struct PluginEntry_t *entry = calloc(1, sizeof(struct PluginEntry_t));

            if (entry)
            {
                entry->pluginInterface = pluginInterface;
                if (ListAdd(PluginsList, entry))
                {
                    printlog(LOG_INFOV, "Loaded plugin %s\n", pluginInterface->name);
                    return 0;
                }
                free(entry);
                return 0;
            }
        }
        else
        {
            printlog(LOG_DEBUGV, "PluginInterface not found for %s.\n", filename);
        }
        lt_dlclose(handle);
    }
    else
    {
        printlog(LOG_DEBUGV, "Failed to open plugin %s - reason %s\n", filename, lt_dlerror());
    }
    return 0;
}

static void PluginInstallPlugin(Plugin_t *pluginInterface)
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
                case PLUGIN_FEATURE_TYPE_DELIVERYMETHOD:
                printlog(LOG_DEBUGV, "plugin %s: Installed Delivery method.\n", pluginInterface->name);
                DeliveryMethodManagerRegister(pluginInterface->features[i].details);
                break;
            }
        }
    }
}

static void PluginUninstallPlugin(Plugin_t *pluginInterface)
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
                case PLUGIN_FEATURE_TYPE_DELIVERYMETHOD:
                printlog(LOG_DEBUG, "plugin %s: Uninstalled Delivery method.\n", pluginInterface->name);
                DeliveryMethodManagerUnRegister(pluginInterface->features[i].details);
                break;
            }
        }
    }
}
