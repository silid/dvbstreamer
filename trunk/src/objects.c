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
#include <pthread.h>

#include "logging.h"
#include "objects.h"

/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/

//#define USE_MALLOC_FOR_ALLOC

#define OBJECTS_ASSERT(_pred, _msg...) \
    do { \
        if (!(_pred)) \
        { \
            LogModule(LOG_ERROR, OBJECT, _msg); \
            exit(1); \
        } \
    }while (0)

#define ObjectToData(_ptr) (void *)(((char*)(_ptr)) + sizeof(Object_t))
#define DataToObject(_ptr) (Object_t *)(((char*)(_ptr)) - sizeof(Object_t))

/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/
typedef enum ClassType_e{
    ClassType_Object,
    ClassType_Collection,
}ClassType_t;

typedef struct Class_s {
    char *name;
    ClassType_t type;
    unsigned int size;
    ObjectDestructor_t destructor;
    unsigned int allocatedCount;
    struct Class_s *next;
}Class_t;

typedef struct Object_s {
#ifdef OBJECT_CHECK
    char    sig[8];
#endif
    Class_t *clazz;
    int32_t refCount;
    uint32_t size;
    struct Object_s *next;
}Object_t;


/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static Class_t *FindClass(char *classname);
static int ObjectRegisterClassType(char *name, ClassType_t type, unsigned size, ObjectDestructor_t destructor);
static void *ObjectAllocInstance(int size, Class_t *clazz);
static void RemoveReferencedObject(Object_t *toRemove);


/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static char OBJECT[] = "Object";
static Class_t *classes = NULL;
static unsigned int classesCount = 0;
static Object_t *referencedObjects = NULL;

static pthread_mutex_t objectMutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
int ObjectInit(void)
{
    classes = NULL;
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
        LogModule(LOG_DEBUG, OBJECT, "Objects with references outstanding:\n");
        for (current = referencedObjects; current; current = next)
         {
            if (current->clazz)
            {
                LogModule(LOG_DEBUG, OBJECT, "\t%p (class %s) (refCount %u)\n", 
                         ObjectToData(current), current->clazz->name, current->refCount);
            }
            else
            {
                LogModule(LOG_DEBUG, OBJECT, "\t%p (size %u) (malloc'ed)\n", ObjectToData(current), current->size);
            }
            next = current->next;
            free(current);
         }
    }

    if (classesCount > 0)
    {
        Class_t *clazz, *next = NULL;
        LogModule(LOG_DEBUG, OBJECT, "%u Registered Classes:\n", classesCount);
        LogModule(LOG_DEBUG, OBJECT, "\tClass Name                       | Size       | Count      |Destructor?\n");
        LogModule(LOG_DEBUG, OBJECT, "\t---------------------------------|------------|------------|------------\n");
        for (clazz = classes; clazz; clazz = next)
        {
            LogModule(LOG_DEBUG, OBJECT, "\t%-32s | %10d | %10d | %s\n", 
                clazz->name, clazz->size, clazz->allocatedCount, clazz->destructor ? "Yes":"No");
            next = clazz->next;
            free(clazz->name);
            free(clazz);
        }
    }
    pthread_mutex_destroy(&objectMutex);

    return OBJECT_OK;
}

int ObjectRegisterClass(char *classname, unsigned int size, ObjectDestructor_t destructor)
{
    int result;
    pthread_mutex_lock(&objectMutex);
    result = ObjectRegisterClassType(classname, ClassType_Object, size, destructor);
    if (result == OBJECT_OK)
    {
        LogModule(LOG_DEBUGV, OBJECT, "Registered Class \"%s\" size %d destructor? %s\n", classname, size, destructor? "Yes":"No");
    }
    pthread_mutex_unlock(&objectMutex);
    return result;
    
}

int ObjectRegisterCollection(char *name, unsigned entrysize, ObjectDestructor_t destructor)
{
    int result;
    pthread_mutex_lock(&objectMutex);
    result = ObjectRegisterClassType(name, ClassType_Collection, entrysize, destructor);
    if (result == OBJECT_OK)
    {
        LogModule(LOG_DEBUGV, OBJECT, "Registered Collection \"%s\" size %d destructor? %s\n", name, entrysize, destructor? "Yes":"No");
    }
    pthread_mutex_unlock(&objectMutex);
    return result;
}

static int ObjectRegisterClassType(char *name, ClassType_t type, unsigned size, ObjectDestructor_t destructor)
{
    Class_t *clazz;

    clazz = FindClass(name);
    if (clazz)
    {
        return OBJECT_ERR_CLASS_REGISTERED;
    }
    clazz = malloc(sizeof(Class_t));
    if (clazz == NULL)
    {
        LogModule(LOG_ERROR, OBJECT, "No space to register class %s", name);
        return OBJECT_ERR_OUT_OF_MEMORY;
    }
    
    
    clazz->name = strdup(name);
    clazz->type = type;
    clazz->size = size;
    clazz->destructor = destructor;
    clazz->allocatedCount = 0;
    clazz->next = classes;
    classes = clazz;
    classesCount ++;
    return OBJECT_OK;
}


void *ObjectCreateImpl(char *classname, char *file, int line)
{
    Class_t *clazz;
    void *result;
    pthread_mutex_lock(&objectMutex);
    clazz = FindClass(classname);

    if (clazz == NULL)
    {
        pthread_mutex_unlock(&objectMutex);
        return NULL;
    }

    result = ObjectAllocInstance(clazz->size, clazz);
    if (result != NULL)
    {
        LogModule(LOG_DEBUGV, OBJECT, "(%p) Created object of class \"%s\" app ptr %p (%s:%d)\n", DataToObject(result), classname, result, file, line);
    }
    else
    {
        LogModule(LOG_ERROR, OBJECT, "Failed to create object of class \"%s\"\n", classname);
    }
    clazz->allocatedCount ++;
    pthread_mutex_unlock(&objectMutex);
    return result;
}

ObjectCollection_t *ObjectCollectionCreateImpl(char *name, unsigned int entries, char *file, int line)
{
    Class_t *clazz;
    ObjectCollection_t *result;
    pthread_mutex_lock(&objectMutex);
    clazz = FindClass(name);

    if (clazz == NULL)
    {
        pthread_mutex_unlock(&objectMutex);
        return NULL;
    }

    result = ObjectAllocInstance(sizeof(ObjectCollection_t) + (clazz->size * entries), clazz);
    if (result != NULL)
    {
        result->nrofEntries = entries;
        LogModule(LOG_DEBUGV, OBJECT, "(%p) Created collection of class \"%s\" entries %d app ptr %p (%s:%d)\n", DataToObject(result), name, entries, result, file, line);
    }
    else
    {
        LogModule(LOG_ERROR, OBJECT, "Failed to create collection of class \"%s\" entries %d\n", name, entries);
    }
    clazz->allocatedCount ++;
    pthread_mutex_unlock(&objectMutex);
    return result;

}

void ObjectRefIncImpl(void *ptr, char *file, int line)
{
    Object_t *object;
    char *clazzName = "<Malloc>";
    pthread_mutex_lock(&objectMutex);
    object = DataToObject(ptr);
    object->refCount ++;
    if (object->clazz)
    {
        clazzName = object->clazz->name;
    }
    LogModule(LOG_DIARRHEA, OBJECT, "(%p:%s) Incrementing ref count, now %d (%s:%d)\n", object, clazzName, object->refCount, file, line);
    pthread_mutex_unlock(&objectMutex);
}

bool ObjectRefDecImpl(void *ptr, char *file, int line)
{
    bool result = TRUE;
    Object_t *object;
    char *clazzName = "<Malloc>";
    if (ptr == NULL)
    {
        LogModule(LOG_ERROR, OBJECT, "Attempt to decrement the reference of NULL! Offending code %s:%d\n", file, line);        
        return FALSE;
    }
    
    pthread_mutex_lock(&objectMutex);
    object = DataToObject(ptr);
#ifdef OBJECT_CHECK
    if (memcmp("ObjectP", object->sig, 8))
    {
        LogModule(LOG_ERROR, OBJECT, "Attempt to decrement the reference of a non-object (%p), offending code %s:%d\n", ptr, file, line);
        exit(1);
    }
#endif
    if (object->clazz)
    {
        clazzName = object->clazz->name;
    }
    
    if (object->refCount > 0)
    {
        object->refCount --;
        LogModule(LOG_DIARRHEA, OBJECT, "(%p:%s) Decrementing ref count, now %d (%s:%d)\n", object, clazzName, object->refCount, file, line);        
    }

    if (object->refCount == 0)
    {
        if (object->clazz)
        {
            LogModule(LOG_DEBUGV, OBJECT, "(%p) Releasing object of class \"%s\" app ptr %p\n",object, object->clazz->name, ptr);
        }
        else
        {
            LogModule(LOG_DEBUGV, OBJECT, "(%p) Releasing malloc'ed size %u app ptr %p\n",object, object->size, ptr);
        }
        /* Call class destructor */
        if (object->clazz && object->clazz->destructor)
        {
            object->clazz->destructor(ptr);            
        }

        if (object->clazz && (object->clazz->size != object->size) && (object->clazz->type == ClassType_Object))
        {
            LogModule(LOG_ERROR, OBJECT, "(%p) Class size != Object size! (class %u object %u)\n", object, object->clazz->size, object->size);
        }
        /* Remove from referenced list */
        RemoveReferencedObject(object);
        memset(ObjectToData(object), 0 , object->size);
        free(object);

        result = FALSE;
    }
    pthread_mutex_unlock(&objectMutex);
    return result;
}

void *ObjectAllocImpl(int size, char *file, int line)
{
    void *result;
#ifdef USE_MALLOC_FOR_ALLOC
    result = malloc(size);
    if (size)
    {
        memset(result, 0, size);
    }
#else
    pthread_mutex_lock(&objectMutex);

    result = ObjectAllocInstance(size, NULL);
    if (result != NULL)
    {
        LogModule(LOG_DEBUGV, OBJECT,"(%p) Malloc'ed memory size %d app ptr %p (%s:%d)\n", DataToObject(result), size, result, file, line);
    }
    pthread_mutex_unlock(&objectMutex);
#endif
    return result;
}

static void *ObjectAllocInstance(int size, Class_t *clazz)
{
    Object_t *result = NULL;

    result = malloc(size + sizeof(Object_t));
    if (!result)
    {
        return NULL;
    }

    memset(ObjectToData(result), 0, size);
#ifdef OBJECT_CHECK
    memcpy(result->sig, "ObjectP", 8);
#endif
    result->clazz = clazz;
    result->size = size;
    result->refCount = 1;
    result->next = referencedObjects;
    referencedObjects = result;
    
    return ObjectToData(result);
}

void ObjectFreeImpl(void *ptr, char *file, int line)
{
#ifdef USE_MALLOC_FOR_ALLOC
    free(ptr);
#else
    Object_t *object = DataToObject(ptr);

    OBJECTS_ASSERT(object->clazz == NULL, "Attempt to free a class based object! (%p class %s)\n", ptr, object->clazz->name);

    OBJECTS_ASSERT(object->refCount == 1, "Attempt to free a memory area with a reference count > 1 (%d)\n", object->refCount);
    ObjectRefDecImpl(ptr, file, line);
#endif    
}

void ObjectDump(void *ptr)
{
    Object_t *object = DataToObject(ptr);
    if (object->clazz)
    {
        LogModule(LOG_DEBUG, OBJECT, "Object(%p) of class \"%s\" app ptr %p ref count %u\n",object, object->clazz->name, ptr, object->refCount);
    }
    else
    {
        LogModule(LOG_DEBUG, OBJECT, "Malloc'ed(%p) size %u app ptr %p ref count %u\n",object, object->size, ptr, object->refCount);
    }
}

bool ObjectIsObject(void *ptr)
{
    Object_t *object;
    Object_t *possibleObject = DataToObject(ptr);

    for (object = referencedObjects; object; object = object->next)
    {
        if (object == possibleObject)
        {
            return TRUE;
        }
    }

    return FALSE;
}

char * ObjectGetObjectClass(void *ptr)
{
    char *classname = NULL;
     Object_t *object = DataToObject(ptr);   
     if (object->clazz)
     {
        classname = object->clazz->name;
     }

    return classname;
}

int ObjectRefCount(void *ptr)
{
    Object_t *object = DataToObject(ptr);
    return object->refCount;
}

/*******************************************************************************
* Local Functions                                                              *
*******************************************************************************/
static Class_t *FindClass(char *classname)
{
    Class_t *clazz;
    for (clazz = classes; clazz;clazz = clazz->next)
     {
        if (strcmp(classname, clazz->name) == 0)
        {
            return clazz;
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
