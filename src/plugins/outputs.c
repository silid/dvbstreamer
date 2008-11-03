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

outputs.c

Outputs Delivery Method handler.

*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>

#include "plugin.h"
#include "ts.h"
#include "deliverymethod.h"
#include "logging.h"
#include "list.h"
#include "properties.h"

/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/


/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/
typedef struct Output_s
{
    char *name;
    unsigned int refCount;
    DeliveryMethodInstance_t *dmInstance;
}Output_t;

typedef struct OutputsState_s
{
    /* !!! MUST BE THE FIRST FIELD IN THE STRUCTURE !!!
     * As the address of this field will be passed to all delivery method
     * functions and a 0 offset is assumed!
     */
    DeliveryMethodInstance_t instance;
    Output_t *output;
}OutputsState_t;


/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
void OutputsInstall(bool installed);
static bool OutputsCanHandle(char *mrl);
static DeliveryMethodInstance_t *OutputsCreate(char *arg);
static void OutputsSendPacket(DeliveryMethodInstance_t *this, TSPacket_t *packet);
static void OutputsSendBlock(DeliveryMethodInstance_t *this, void *block, unsigned long blockLen);
static void OutputsDestroy(DeliveryMethodInstance_t *this);

static void CommandAddOutput(int argc, char **argv);
static void CommandRemoveOutput(int argc, char **argv);

static void OutputDestructor(void *arg);
static int OutputPropertyMRLGet(void *userArg, PropertyValue_t *value);
static int OutputPropertyMRLSet(void * userArg, PropertyValue_t * value);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
/** Constants for the start of the MRL **/
#define PREFIX_LEN (sizeof(OutputPrefix) - 1)
const char OutputPrefix[] = "out://";

/** Plugin Interface **/
DeliveryMethodHandler_t OutputsHandler = {
            OutputsCanHandle,
            OutputsCreate
        };

const char OUTPUTS[] = "Outputs";

static pthread_mutex_t outputsMutex = PTHREAD_MUTEX_INITIALIZER;
static List_t *outputsList;
static char *propertiesParent = "outputs";
/*******************************************************************************
* Plugin Setup                                                                 *
*******************************************************************************/

PLUGIN_FEATURES(
    PLUGIN_FEATURE_INSTALL(OutputsInstall),
    PLUGIN_FEATURE_DELIVERYMETHOD(OutputsHandler)
);

PLUGIN_COMMANDS(
    {
        "addoutput",
        TRUE, 1, 2,
        "Add a new output.",
        "addoutput <output> [<mrl>]\n"
        "Add a new output that can be used by multiple filters. \n"
        "An optional initial mrl can be specified, otherwise the default mrl is null://."
        "To change the mrl for the new output use setprop outputs.<output> <new mrl>",
        CommandAddOutput
    },
    {
        "rmoutput",
        TRUE, 1, 1,
        "Remove an output.",
        "rmoutput <output>\n"
        "Remove an output, note that the output must not currently be in use to be able to remove it.",
        CommandRemoveOutput
    }
);


PLUGIN_INTERFACE_CF(
    PLUGIN_FOR_ALL,
    "Outputs",
    "0.1",
    "Multifilter Outputs Delivery methods.\n"
    "Use output://[output name] for to send packets to the specified output.\n",
    "charrea6@users.sourceforge.net"
);

void OutputsInstall(bool installed)
{
    if (installed)
    {
        ObjectRegisterTypeDestructor(Output_t, OutputDestructor);
        outputsList = ObjectListCreate();
        PropertiesAddProperty("", propertiesParent, "Branch containing all created outputs", PropertyType_None, NULL, NULL, NULL);
    }
    else
    {
        PropertiesRemoveAllProperties(propertiesParent);
        ObjectListFree(outputsList)
    }
}

/*******************************************************************************
* Delivery Method Functions                                                    *
*******************************************************************************/
static bool OutputsCanHandle(char *mrl)
{
    return (strncmp(OutputPrefix, mrl, PREFIX_LEN) == 0);
}

static DeliveryMethodInstance_t *OutputsCreate(char *arg)
{
    OutputsState_t *state = NULL;
    Output_t *output = NULL;
    ListIterator_t iterator;
    char *outputName = arg + PREFIX_LEN;

    pthread_mutex_lock(&outputsMutex);
    for (ListIterator_Init(iterator, outputsList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        Output_t *currentOutput = (Output_t*)ListIterator_Current(iterator);
        if (strcmp(outputName, currentOutput->name) == 0)
        {
            output = currentOutput;
            output->refCount ++;
            break;
        }
    }
    pthread_mutex_unlock(&outputsMutex);


    if (output == NULL)
    {
        LogModule(LOG_DEBUG, OUTPUTS, "Failed to find output %s\n", outputName);
        return NULL;
    }


    state = calloc(1, sizeof(OutputsState_t));
    if (state == NULL)
    {
        LogModule(LOG_DEBUG, OUTPUTS, "Failed to allocate Outputs state\n");
        return NULL;
    }
    state->instance.SendPacket = OutputsSendPacket;
    state->instance.SendBlock = OutputsSendBlock;
    state->instance.DestroyInstance = OutputsDestroy;

    return &state->instance;
}

static void OutputsDestroy(DeliveryMethodInstance_t *this)
{
    OutputsState_t *state = (OutputsState_t *)this;
    pthread_mutex_lock(&outputsMutex);
    state->output->refCount --;
    pthread_mutex_unlock(&outputsMutex);
    free(state);
}

static void OutputsSendPacket(DeliveryMethodInstance_t *this, TSPacket_t *packet)
{
    OutputsState_t *state = (OutputsState_t *)this;
    pthread_mutex_lock(&outputsMutex);
    state->output->dmInstance->SendPacket(state->output->dmInstance, packet);
    pthread_mutex_unlock(&outputsMutex);
}

static void OutputsSendBlock(DeliveryMethodInstance_t *this, void *block, unsigned long blockLen)
{
    OutputsState_t *state = (OutputsState_t *)this;
    pthread_mutex_lock(&outputsMutex);
    state->output->dmInstance->SendBlock(state->output->dmInstance, block, blockLen);
    pthread_mutex_unlock(&outputsMutex);
}

/*******************************************************************************
* Command functions                                                            *
*******************************************************************************/
static void CommandAddOutput(int argc, char **argv)
{
    Output_t *output = NULL;
    ListIterator_t iterator;
    pthread_mutex_lock(&outputsMutex);
    for (ListIterator_Init(iterator, outputsList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        Output_t *currentOutput = (Output_t*)ListIterator_Current(iterator);
        if (strcmp(argv[0], currentOutput->name) == 0)
        {
            output = currentOutput;
            CommandError(COMMAND_ERROR_GENERIC, "Another output with that name already exists!");
            break;
        }
    }

    if (output == NULL)
    {
        output = ObjectCreateType(Output_t);
        if (output)
        {
            char *mrl = "null://";
            output->name = strdup(argv[0]);
            if (output->name)
            {
                output->dmInstance = DeliveryMethodCreate(mrl);
                if (output->dmInstance)
                {
                    char propertyPath[PROPERTIES_PATH_MAX];
                    sprintf(propertyPath, "%s.%s", propertiesParent, output->name);
                    PropertiesAddProperty(propertyPath, "mrl", "The destination all packets sent to this output will be routed to.", PropertyType_String, output, OutputPropertyMRLGet, OutputPropertyMRLSet);
                    PropertiesAddProperty(propertyPath, "refcount", "The number of mrls referencing this output.", PropertyType_Int, &output->refCount, PropertiesSimplePropertyGet, NULL);
                    ListAdd(outputsList, output);
                }
                else
                {
                    ObjectRefDec(output);
                    CommandError(COMMAND_ERROR_GENERIC, "Failed to create mrl for output.");

                }
            }
            else
            {
                ObjectRefDec(output);
                CommandError(COMMAND_ERROR_GENERIC, "Failed to allocate memory for output name.");
            }
        }
        else
        {
            CommandError(COMMAND_ERROR_GENERIC, "Failed to allocate memory for output.");
        }
    }
    pthread_mutex_unlock(&outputsMutex);
}

static void CommandRemoveOutput(int argc, char **argv)
{
    ListIterator_t iterator;

    pthread_mutex_lock(&outputsMutex);
    for (ListIterator_Init(iterator, outputsList); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        Output_t *currentOutput = (Output_t*)ListIterator_Current(iterator);
        if (strcmp(argv[0], currentOutput->name) == 0)
        {
            if (currentOutput->refCount == 0)
            {
                char propertyPath[PROPERTIES_PATH_MAX];
                ListRemoveCurrent(&iterator);
                sprintf(propertyPath, "%s.%s", propertiesParent, currentOutput->name);
                PropertiesRemoveAllProperties(propertyPath);
                ObjectRefDec(currentOutput);
            }
            else
            {
                CommandError(COMMAND_ERROR_GENERIC, "Output still in use!");
            }

            break;
        }
    }
    pthread_mutex_unlock(&outputsMutex);
}

/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/
static void OutputDestructor(void *arg)
{
    Output_t *output = arg;
    if (output->name != NULL)
    {
        free(output->name);
    }

    if (output->dmInstance)
    {
        DeliveryMethodDestroy(output->dmInstance);
    }
}

static int OutputPropertyMRLGet(void *userArg, PropertyValue_t *value)
{
    Output_t *output = (Output_t *) userArg;
    value->u.string = strdup(output->dmInstance->mrl);
    return 0;
}

static int OutputPropertyMRLSet(void * userArg, PropertyValue_t * value)
{
    int result = -1;
    Output_t *output = (Output_t *) userArg;
    DeliveryMethodInstance_t *newDMInstance = NULL;
    newDMInstance = DeliveryMethodCreate(value->u.string);
    if (newDMInstance)

    {
        DeliveryMethodDestroy(output->dmInstance);
        output->dmInstance = newDMInstance;
        result = 0;
    }
    return result;
}

