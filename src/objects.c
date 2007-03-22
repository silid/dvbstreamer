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

objects.c

Object memory management.

*/
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <assert.h>

#include "logging.h"
#include "objects.h"

/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/
#define MAX_CLASSES 50

#define OBJECTS_ASSERT(_pred, _msg...) \
    do { \
        if (!(_pred)) \
        { \
            printlog(LOG_ERROR, _msg); \
            exit(1); \
        } \
    }while (0)

#define ObjectToData(_ptr) (void *)(((char*)(_ptr)) + sizeof(Object_t))
#define DataToObject(_ptr) (Object_t *)(((char*)(_ptr)) - sizeof(Object_t))


/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/
typedef struct Class_s {
    char *name;
    unsigned int size;
    ObjectDestructor_t destructor;
}Class_t;

typedef struct Object_s {
    Class_t *clazz;
    uint32_t refCount;
    uint32_t size;
    struct Object_s *next;
}Object_t;


/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static Class_t *FindClass(char *classname);
static void RemoveReferencedObject(Object_t *toRemove);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static Class_t classes[MAX_CLASSES];
static unsigned int classesCount = 0;
static Object_t *referencedObjects = NULL;

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
int ObjectInit(void)
{
    memset(classes, 0, sizeof(classes));
    classesCount = 0;
    referencedObjects = NULL;

    return OBJECT_OK;
}

int ObjectDeinit(void)
{
    if (referencedObjects)
    {
        Object_t *current; 
        Object_t *next;
        printlog(LOG_DEBUG, "Objects with references outstanding:\n");
        for (current = referencedObjects; current; current = next)
         {
            if (current->clazz)
            {
                printlog(LOG_DEBUG, "\t%p (class %s) (refCount %u)\n", 
                         ObjectToData(current), current->clazz->name, current->refCount);
            }
            else
            {
                printlog(LOG_DEBUG, "\t%p (size %u) (malloc'ed)\n", ObjectToData(current), current->size);
            }
            next = current->next;
            free(current);
         }
    }

    if (classesCount > 0)
    {
        unsigned int i;
        printlog(LOG_DEBUG, "Registered Classes:\n");
        for (i = 0; i < classesCount; i ++)
        {
            printlog(LOG_DEBUG, "\t%s size %d destructor? %s\n", classes[i].name, classes[i].size, classes[i].destructor ? "Yes":"No");
        }
    }

    return OBJECT_OK;
}

int ObjectRegisterClass(char *classname, unsigned int size, ObjectDestructor_t destructor)
{
    Class_t *clazz = FindClass(classname);

    if (clazz)
    {
        return OBJECT_ERR_CLASS_REGISTERED;
    }
    
    if (classesCount >= MAX_CLASSES)
    {
        return OBJECT_ERR_OUT_OF_MEMORY;
    }
    
    classes[classesCount].name = classname;
    classes[classesCount].size = size;
    classes[classesCount].destructor = destructor;
    classesCount ++;
    printlog(LOG_DIARRHEA, "Registered class \"%s\" size %d destructor? %s\n", classname, size, destructor? "Yes":"No");
    return OBJECT_OK;
}

void *ObjectCreate(char *classname)
{
    Class_t *clazz = FindClass(classname);
    Object_t *object;
    void *result;

    if (clazz == NULL)
    {
        return NULL;
    }
    

    result = ObjectAlloc(clazz->size);
    if (result == NULL)
    {
        return NULL;
    }
    object = DataToObject(result);
    object->clazz = clazz;
    printlog(LOG_DIARRHEA, "Created object(%p) of class \"%s\" app ptr %p\n", object, classname, result);
    return result;
}

void ObjectRefInc(void *ptr)
{
    Object_t *object = DataToObject(ptr);
    object->refCount ++;
}

bool ObjectRefDec(void *ptr)
{
    Object_t *object = DataToObject(ptr);

    if (object->refCount > 0)
    {
        object->refCount --;
    }

    if (object->refCount == 0)
    {
        if (object->clazz)
        {
            printlog(LOG_DIARRHEA, "Releasing object(%p) of class \"%s\" app ptr %p\n",object, object->clazz->name, ptr);
        }
        else
        {
            printlog(LOG_DIARRHEA, "Releasing malloc'ed(%p) size %u app ptr %p\n",object, object->size, ptr);
        }
        /* Call class destructor */
        if (object->clazz && object->clazz->destructor)
        {
            object->clazz->destructor(ptr);            
        }
        
        /* Remove from referenced list */
        RemoveReferencedObject(object);
        memset(object, 0 , object->size);
        free(object);
        return FALSE;
    }

    return TRUE;
}

void *ObjectAlloc(int size)
{
    Object_t *result = NULL;

    result = malloc(size + sizeof(Object_t));
    if (!result)
    {
        return NULL;
    }

    memset(ObjectToData(result), 0, size);

    result->clazz = NULL;
    result->size = size;
    result->refCount = 1;
    result->next = referencedObjects;
    referencedObjects = result;

    return ObjectToData(result);
}

void ObjectFree(void *ptr)
{
    Object_t *object = DataToObject(ptr);

    OBJECTS_ASSERT(object->clazz == NULL, "Attempt to free a class based object! (%p class %s)\n", ptr, object->clazz->name);

    OBJECTS_ASSERT(object->refCount == 1, "Attempt to free a memory area with a reference count > 1 (%d)\n", object->refCount);
    
    ObjectRefDec(ptr);
}

void ObjectDump(void *ptr)
{
    Object_t *object = DataToObject(ptr);
    if (object->clazz)
    {
        printlog(LOG_DEBUG, "Object(%p) of class \"%s\" app ptr %p ref count %u\n",object, object->clazz->name, ptr, object->refCount);
    }
    else
    {
        printlog(LOG_DEBUG, "Malloc'ed(%p) size %u app ptr %p ref count %u\n",object, object->size, ptr, object->refCount);
    }
}
/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/
static Class_t *FindClass(char *classname)
{
    unsigned i;
    for (i = 0; i < classesCount;i ++)
     {
        if (strcmp(classname, classes[i].name) == 0)
        {
            return &classes[i];
        }
     }
     return NULL;
}

static void RemoveReferencedObject(Object_t *toRemove)
{
    Object_t *current = referencedObjects;
    Object_t *previous = NULL;

    while(current)
    {
        if (current == toRemove)
        {
            if (previous)
            {
                previous->next = current->next;
            }
            else
            {
                referencedObjects = current->next;
            }
            break;
        }
        previous = current;
        current = current->next;
    }
}

