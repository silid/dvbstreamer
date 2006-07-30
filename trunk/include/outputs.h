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

Outputs.h

Additional output management functions.

*/

#ifndef _OUTPUTS_H
#define _OUTPUTS_H
/**
 * @defgroup Outputs Output Management
 * @{
 */
/**
 * Maximum number of outputs
 */
#define MAX_OUTPUTS (MAX_FILTERS - PIDFilterIndex_Count)

/**
 * Enum representing the different types of output.
 */
typedef enum OutputType
{
    OutputType_Manual, /**< Manual Output, user assigns PIDs to be filtered. */
    OutputType_Service /**< Service Tracking output, filter keeps track of services PMT */
}OutputType;

/**
 * Structure representing an Output.
 */
typedef struct Output_t
{
    char *name;          /**< Name of the output */
    OutputType type;     /**< Type of the output */
    PIDFilter_t *filter; /**< The filter assigned to the output */
}
Output_t;

/**
 * String describing the reason for an operations failure.
 */
extern char *OutputErrorStr;

/**
 * List of manual outputs.
 */
extern List_t *ManualOutputsList;

/**
 * List of service filter outputs.
 */
extern List_t *ServiceOutputsList;

/**
 * Initialises the Outputs module.
 */
int OutputsInit();

/**
 * Deinitialise the Outputs module.
 * This involves stopping and free all the outputs.
 */
void OutputsDeInit();

/**
 * Allocate a new output of the specified type and name.
 * @param name Name of the new output, this must be unique to the output type.
 * @param type Type of the new output.
 * @param destination Destination to send the packets filtered by this output.
 * @return an Output_t instance or NULL if an error occured.
 */
Output_t *OutputAllocate(char *name, OutputType type, char *destination);

/**
 * Stops the filter and frees the output.
 * @param output The output to free.
 */
void OutputFree(Output_t *output);

/**
 * Find an output by name and type.
 * @param name The name of the output to find.
 * @param type The type of the output to find.
 * @return An existing Output_t instance or NULL if is not found,
 */
Output_t *OutputFind(char *name, OutputType type);

/**
 * Add a PID to a Manual output.
 * @param output The output to add to.
 * @param pid    The PID to add.
 * @return 0 on success, non 0 on failure.
 */
int OutputAddPID(Output_t *output, uint16_t pid);

/**
 * Remove a PID from a Manual output.
 * @param output The output to remove from.
 * @param pid    The PID to remove.
 * @return 0 on success, non 0 on failure.
 */
int OutputRemovePID(Output_t *output, uint16_t pid);

/**
 * Get the PIDs assigned to the output.
 * @param output The output to retrieve the list of PIDs from.
 * @param pidCount Used to store the number of PIDs assigned to this output.
 * @param pids Used to store an array containing the PID values. Must be free'd by the caller.
 * @return 0 on success, non 0 on failure.
 */
int OutputGetPIDs(Output_t *output, int *pidcount, uint16_t **pids);

/**
 * Set the service on a Service Output.
 * @param output  The output to set the service of.
 * @param service The service to set.
  * @return 0 on success, non 0 on failure.
 */
int OutputSetService(Output_t *output, Service_t *service);

/**
 * Retrieve the current service of the output.
 * @param output The output to get the current service of.
 * @param service Used to store the current service of the output.
 * @return 0 on success, non 0 on failure.
 */
int OutputGetService(Output_t *output, Service_t **service);

/** @} */
#endif

