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
#include "stdint.h"

#define PROPERTIES_PATH_MAX 255

#define PROPERTIES_TABLE_COLUMNS_MAX 10

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

typedef struct PropertyInfo_s
{
    char *name;
    char *desc;
    PropertyType_e type;
    bool readable;
    bool writeable;
    bool hasChildren;
}PropertyInfo_t;

typedef int (*PropertySimpleAccessor_t)(void *userArg, PropertyValue_t *value);


#define PROPERTY_MAX_PATH_ELEMENTS 256

typedef struct PropertyPathElements_s
{
    int nrofElements;
    char *elements[PROPERTY_MAX_PATH_ELEMENTS];
}PropertyPathElements_t;

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


int PropertiesAddProperty(const char *path, const char *name, const char *desc, PropertyType_e type, 
                              void *userArg, PropertySimpleAccessor_t get, PropertySimpleAccessor_t set);

int PropertiesRemoveProperty(const char *path, const char *name);

int PropertiesRemoveAllProperties(const char *path);

int PropertiesSet(char *path, PropertyValue_t *value);

int PropertiesGet(char *path, PropertyValue_t *value);

int PropertiesSetStr(char *path, char *value);

int PropertiesEnumerate(char *path, PropertiesEnumerator_t *pos);

PropertiesEnumerator_t PropertiesEnumNext(PropertiesEnumerator_t pos);

void PropertiesEnumGetInfo(PropertiesEnumerator_t pos, PropertyInfo_t *propInfo);

int PropertiesGetInfo(char *path, PropertyInfo_t *propInfo);

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

/**
 * Mode flag indicating the simple property is readable.
 */
#define SIMPLEPROPERTY_R    1
/**
 * Mode flag indicating the simple property is writeable.
 */
#define SIMPLEPROPERTY_W    2
/**
 * Mode flag indicating the simple property is both readable and writeable.
 */
#define SIMPLEPROPERTY_RW   (SIMPLEPROPERTY_R|SIMPLEPROPERTY_W)
/**
 * Helper macro to a add simple property accessor.
 * @param path Path to the parent to add this property to.
 * @param name Name of the property to add.
 * @param desc Description of this property.
 * @param type Type of this property.
 * @param valueptr Pointer to the location the value of this property is stored.
 * @param mode Whether this property is readable/writable (see SIMPLEPROPTERY_[R/W/RW]).
 * @return 0 on success, 1 otherwise.
 */
#define PropertiesAddSimpleProperty(path, name, desc, type, valueptr, mode) \
    PropertiesAddProperty(path, name, desc, type, valueptr, \
            ((mode) & SIMPLEPROPERTY_R)?PropertiesSimplePropertyGet:NULL,\
            ((mode) & SIMPLEPROPERTY_W)?PropertiesSimplePropertySet:NULL)
#endif

