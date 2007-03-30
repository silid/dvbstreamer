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

Objects.h

Object memory management.

*/

#ifndef _OBJECTS_H
#define _OBJECTS_H
#include "types.h"

/**
 * @defgroup Object Object Memory Managment functions.
 *@{
 */
 
#define OBJECT_OK                   0 /**< OK (No Error). */      
#define OBJECT_ERR_OUT_OF_MEMORY    1 /**< Out of memory. */
#define OBJECT_ERR_CLASS_REGISTERED 2 /**< Class already registered. */
#define OBJECT_ERR_CLASS_NOT_FOUND  3 /**< Class could not be found. */
/**
 * Type for a function to call when free'ing an object.
 * ptr is the pointer to the object being freed.
 */
typedef void (*ObjectDestructor_t)(void *ptr);

/**
 * Initialise object memory system.
 * @returns 0 on success.
 */
int ObjectInit(void);
/**
 * Deinitialise object memory system.
 * @returns 0 on success.
 */
int ObjectDeinit(void);

/**
 * Register a class of object to use with the ObjectCreate function.
 * @param classname Name of the class being registered.
 * @param size Size of the memory need for the object.
 * @param destructor Function to call when the reference count reach zero for an
 * object. This is only required if the object includes references to other 
 * object/malloc memory that needs to be freed.
 * @return 0 on success.
 */
int ObjectRegisterClass(char *classname, unsigned int size, ObjectDestructor_t destructor);

/**
 * Helper macro to register a type as an object class.
 * @param _type The type to register with the object system.
 */
#define ObjectRegisterType(_type) ObjectRegisterClass(TOSTRING(_type), sizeof(_type), NULL)

/**
 * Create a new object of class <classname>. The initial reference count for the
 * returned object will be 1.
 * @param classname Class of object to create.
 * @return A pointer to the new object or NULL.
 */
void *ObjectCreateImpl(char *classname, char *file, int line);
 #define ObjectCreate(_classname) ObjectCreateImpl(_classname, __FILE__, __LINE__)
 
/**
 * Helper macro to create a new object of the specfied type.
 * @param _type The new type of the object to create.
 */
#define ObjectCreateType(_type) ObjectCreate(TOSTRING(_type))

/**
 * Increment the reference count for the specified object.
 * @param ptr Pointer to the object to increment the ref. count of.
 */
void ObjectRefIncImpl(void *ptr, char *file, int line);
#define ObjectRefInc(_ptr) ObjectRefIncImpl(_ptr, __FILE__, __LINE__)

/**
 * Decrement the reference count for the specified object. If the reference count
 * reaches zero the object's class's destructor will be called and the memory freed.
 * @param ptr Pointer to the object to decrement the ref. count of.
 * @return True if there are more references to the object, false if not.
 */
bool ObjectRefDecImpl(void *ptr, char *file, int line);
#define ObjectRefDec(_ptr) ObjectRefDecImpl(_ptr, __FILE__, __LINE__)

/**
 * Replacement for malloc, with the addition that it also clears the memory to zero.
 * @param size Size of the block to allocated.
 * @return Pointer to the new memory block or NULL.
 */
void *ObjectAlloc(int size);

/**
 * Replacement for free. Releases memory previously allocated by ObjectAlloc.
 * @param ptr The memory block to free.
 */
void ObjectFree(void *ptr);

/**
 * Print out debugging information about the object.
 * @param ptr Pointer to the object to dump information on.
 */
void ObjectDump(void *ptr);

/** @} */
#endif
