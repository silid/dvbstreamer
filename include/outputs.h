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

#define MAX_OUTPUTS (MAX_FILTERS - PIDFilterIndex_Count)

typedef enum OutputType
{
    OutputType_Manual,
    OutputType_Service
}OutputType;

typedef struct Output_t
{
    char *name;
    OutputType type;
    PIDFilter_t *filter;
}
Output_t;

extern char *OutputErrorStr;
extern Output_t Outputs[];

int OutputsInit();
void OutputsDeInit();

Output_t *OutputAllocate(char *name, OutputType type, char *destination);
void OutputFree(Output_t *output);
Output_t *OutputFind(char *name, OutputType type);
int OutputAddPID(Output_t *output, uint16_t pid);
int OutputRemovePID(Output_t *output, uint16_t pid);
int OutputGetPIDs(Output_t *output, int *pidcount, uint16_t **pids);
int OutputSetService(Output_t *output, Service_t *service);
int OutputGetService(Output_t *output, Service_t **service);
#endif

