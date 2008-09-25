/*
Copyright (C) 2008  Adam Charrett

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

properties.h

Expose internal properties to the user.

*/
#ifndef _PROPERTIES_H
#define _PROPERTIES_H

#include "types.h"

#define PROPERTIES_PATH_MAX 255

#define PROPERTIES_TABLE_DIMS_MAX 4

typedef enum PropertyType_e {
    PropertyType_None,      /**< Special internal type, do not use. */
    /* Standard types */
    PropertyType_Int,       /**< 32bit Integer */
    PropertyType_Float,     /**< Floating point (double)*/
    PropertyType_Boolean,   /**< True/False Value */
    PropertyType_String,    /**< Null terminated string */
    PropertyType_Char,      /**< Single character */
    /* Special types */    
    PropertyType_PID,       /**< 12Bit unsigned integer, special value of 8192 allowed for all PIDs*/
    PropertyType_IPAddress, /**< IPv4 or IPv6 IP address/hostname */
    PropertyType_Table,     /**< Multi-dimensional tables, need to use VarTableDescription_t to describe the table */    
}PropertyType_e;

typedef void *PropertiesEnumerator_t;
#define  PropertiesEnumMoreEntries(_pos) ((_pos) != NULL)

typedef struct PropertyValue_s{
    PropertyType_e type;
    union {
        int integer;
        double fp;
        bool boolean;
        char *string;
        char ch;
        uint16_t pid;
    }u;
}PropertyValue_t;
    
typedef struct PropertyTableDimension_s{
    char *description;
}PropertyTableDimension_t;

typedef struct PropertyTable_s{
    int nrofDimensions; /**< Number of dimensions in this table */
    PropertyTableDimension_t *dimensions; /**< Description of the table dimensions */
}PropertyTable_t;

typedef struct PropertyTableLocation_s{
    int nrofDimensions;
    char *indices[PROPERTIES_TABLE_DIMS_MAX];
}PropertyTableLocation_t;

typedef int (*PropertySimpleAccessor_t)(void *userArg, PropertyValue_t *value);
typedef int (*PropertyTableAccessor_t)(void *userArg, PropertyTableLocation_t *location, PropertyValue_t *value);
typedef int (*PropertyTableCounter_t)(void *userArg, PropertyTableLocation_t *location);

typedef struct PropertyDef_s{
    char *name;
    char *description;
    PropertyType_e type;
    void *userArg;
    union {
        struct {
            PropertySimpleAccessor_t set;
            PropertySimpleAccessor_t get;
        }simple;
        struct {
            PropertyTableAccessor_t set;
            PropertyTableAccessor_t get;
            PropertyTableCounter_t  count;
        }table;
    }accessors;
}PropertyDef_t;

/**
 * Initialise properties module.
 * @return 0 on success.
 */
int PropertiesInit(void);

/**
 * De-initialise properties module.
 * @return 0 on success.
 */
int PropertiesDeInit(void);


int PropertiesAddProperty(char *path, char *name, char *desc, PropertyType_e type, 
                              void *userArg, PropertySimpleAccessor_t get, PropertySimpleAccessor_t set);

int PropertyAddTableProperty(char *path, char *name, char *desc, 
                              void *userArg, PropertyTableAccessor_t get, PropertyTableAccessor_t set, 
                              PropertyTableCounter_t count);

int PropertiesRemoveProperty(char *path, char *name);

int PropertiesRemoveAllProperties(char *path);

int PropertiesSet(char *path, PropertyValue_t *value);

int PropertiesGet(char *path, PropertyValue_t *value);

int PropertiesSetStr(char *path, char *value);

int PropertiesEnumerate(char *path, PropertiesEnumerator_t *pos);
PropertiesEnumerator_t PropertiesEnumNext(PropertiesEnumerator_t pos);
void PropertiesEnumGetInfo(PropertiesEnumerator_t pos, char **name, char **desc, PropertyType_e *type, bool *get, bool *set, bool *branch);

int PropertiesGetInfo(char *path, char **desc, PropertyType_e *type, bool *get, bool *set, bool *branch);

/**
 * Simple properties getter that returns the value stored at userArg.
 * For use as the get parameter in ProperiesAddProperty for simple values that
 * don't need computing or locking.
 *
 * @param userArg Pointer to value to return. (for strings this must be the address of the pointer to the string (ie char**))
 * @param value Pointer to location to store value.
 * @return 0 if the property type was supported, -1 otherwise.
 */
int PropertiesSimplePropertyGet(void *userArg, PropertyValue_t *value);

/**
 * Simple properties setter that sets the value stored at userArg.
 * For use as the set parameter in ProperiesAddProperty for simple values that
 * don't need computing or locking.
 *
 * @param userArg Pointer to value to set. (for strings this must be the address of the pointer to the string (ie char**))
 * @param value Pointer to location to store value.
 * @return 0 if the property type was supported, -1 otherwise.
 */
int PropertiesSimplePropertySet(void *userArg, PropertyValue_t *value);
#endif

