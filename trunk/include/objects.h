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
 * @defgroup Object Object Memory Managment
 *@{
 */
 
#define OBJECT_OK                   0 /**< OK (No Error). */      
#define OBJECT_ERR_OUT_OF_MEMORY    1 /**< Out of memory. */
#define OBJECT_ERR_CLASS_REGISTERED 2 /**< Class already registered. */
#define OBJECT_ERR_CLASS_NOT_FOUND  3 /**< Class could not be found. */

/**
 * A collection of entries with a count.
 */
typedef struct ObjectCollection_s{
    union{
    unsigned int nrofEntries; /**< Number of entries in the collection */
    void *__forceAlignment; /* Used to fix issues of alignment on 64bit systems */
    };
}ObjectCollection_t;

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
 * Register a new of object collection to use with the ObjectCollectionCreate function.
 * @param classname Name of the collection being registered.
 * @param size Size of the memory need for the object.
 * @param destructor Function to call when the reference count reach zero for an
 * object. This is only required if the object includes references to other 
 * object/malloc memory that needs to be freed.
 * @return 0 on success.
 */

int ObjectRegisterCollection(char *name, unsigned entrysize,  ObjectDestructor_t destructor);

/**
 * Helper macro to register a type as an object class.
 * @param _type The type to register with the object system.
 */
#define ObjectRegisterType(_type) ObjectRegisterClass(TOSTRING(_type), sizeof(_type), NULL)

/**
 * Helper macro to register a type as an object class.
 * @param _type The type to register with the object system.
 * @param _destructor Function to call when free'ing an object of the type being
 * registered.
 */
#define ObjectRegisterTypeDestructor(_type, _destructor) ObjectRegisterClass(TOSTRING(_type), sizeof(_type), _destructor)

/**
 * Create a new object of class \<classname\>. The initial reference count for the
 * returned object will be 1.
 * @param _classname Class of object to create.
 * @return A pointer to the new object or NULL.
 */
#define ObjectCreate(_classname) ObjectCreateImpl(_classname, __FILE__, __LINE__)

/**
 * Create a new collection of class \<classname\>. The initial reference count for the
 * returned object will be 1.
 * @param _classname Class of object to create.
 * @param _entries The number of entries in the collection.
 * @return A pointer to the new object or NULL.
 */
#define ObjectCollectionCreate(_classname, _entries) ObjectCollectionCreateImpl(_classname, _entries,__FILE__, __LINE__)

/**
 * Create a new object of class \<classname\>. The initial reference count for the
 * returned object will be 1.
 *
 * @note This function should not be used instead the macro ObjectCreate should be used.
 *
 * @param classname Class of object to create.
 * @param file The file this function is being called from.
 * @param line The line this function is being called from.
 * @return A pointer to the new object or NULL.
 */
void *ObjectCreateImpl(char *classname, char *file, int line);
 
/**
 * Helper macro to create a new object of the specfied type.
 * @param _type The new type of the object to create.
 */
#define ObjectCreateType(_type) ObjectCreate(TOSTRING(_type))

/**
 * Create a new collection of class \<classname\>. The initial reference count for the
 * returned object will be 1.
 *
 * @note This function should not be used instead the macro ObjectCollectionCreate should be used.
 *
 * @param classname Class of object to create.
 * @param entries The number of entries the collection should contain.
 * @param file The file this function is being called from.
 * @param line The line this function is being called from.
 * @return A pointer to the new object or NULL.
 */
ObjectCollection_t *ObjectCollectionCreateImpl(char *classname, unsigned int entries, char *file, int line);


/**
 * Increment the reference count for the specified object.
 * @param _ptr Pointer to the object to increment the ref. count of.
 */
#define ObjectRefInc(_ptr) ObjectRefIncImpl(_ptr, __FILE__, __LINE__)

/**
 * Increment the reference count for the specified object.
 * @note This function should not be used instead the macro ObjectRefInc should be used. 
 * @param ptr Pointer to the object to increment the ref. count of.
 * @param file The file this function is being called from.
 * @param line The line this function is being called from. 
 */
void ObjectRefIncImpl(void *ptr, char *file, int line);

/**
 * Decrement the reference count for the specified object. If the reference count
 * reaches zero the object's class's destructor will be called and the memory freed.
 * @param _ptr Pointer to the object to decrement the ref. count of.
 * @return True if there are more references to the object, false if not.
 */
#define ObjectRefDec(_ptr) ObjectRefDecImpl(_ptr, __FILE__, __LINE__)

/**
 * Decrement the reference count for the specified object. If the reference count
 * @note This function should not be used instead the macro ObjectRefDec should be used. 
 * @param ptr Pointer to the object to decrement the ref. count of.
 * @param file The file this function is being called from.
 * @param line The line this function is being called from. 
 */
bool ObjectRefDecImpl(void *ptr, char *file, int line);

/**
 * Retrieve the current reference count of the specific object.
 * @param ptr Pointer to the object to get the reference count of.
 * @return The reference count of the object.
 */
int ObjectRefCount(void *ptr);

/**
 * Queries the supplied pointer to determine if it is an object instance.
 * @param ptr Pointer to check to see if it is an object pointer.
 * @return True if the pointer is an object pointer, false otherwise.
 */
bool ObjectIsObject(void *ptr);

/**
 * Queries the supplied object pointer to determine the class of the object.
 * @param ptr Pointer to check.
 * @return The name of the class of the object or NULL if a alloc'ed block.
 */
char *ObjectGetObjectClass(void * ptr);

/**
 * Replacement for malloc, with the addition that it also clears the memory to zero.
 * @param _size Size of the block to allocated.
 * @return Pointer to the new memory block or NULL.
 */
#define ObjectAlloc(_size) ObjectAllocImpl(_size, __FILE__, __LINE__)

/**
 * Replacement for malloc  with the addition that it also clears the memory to zero.
 * @note This function should not be used instead the macro ObjectAlloc should be used. 
 * @param size Size of the block to allocated.
 * @param file The file this function is being called from.
 * @param line The line this function is being called from.  
 * @return Pointer to the new memory block or NULL.
 */
void *ObjectAllocImpl(int size, char * file, int line);

/**
 * Replacement for free. Releases memory previously allocated by ObjectAlloc.
 * @param _ptr The memory block to free.
 */
#define ObjectFree(_ptr) ObjectFreeImpl(_ptr, __FILE__, __LINE__)

/**
 * Replacement for free. Releases memory previously allocated by ObjectAlloc.
 * @note This function should not be used instead the macro ObjectFree should be used.  
 * @param _ptr The memory block to free.
 * @param file The file this function is being called from.
 * @param line The line this function is being called from.   
 */
void ObjectFreeImpl(void * ptr, char * file, int line);

/**
 * Print out debugging information about the object.
 * @param ptr Pointer to the object to dump information on.
 */
void ObjectDump(void *ptr);

/** @} */
#endif
