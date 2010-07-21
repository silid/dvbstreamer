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

properties.c

Expose internal properties to the user.

*/
#include "config.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include <getopt.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>

#include "objects.h"
#include "logging.h"
#include "properties.h"


/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/
typedef struct PropertyNode_s {
    struct PropertyNode_s *parent;
    struct PropertyNode_s *next;
    
    const char *name;
    const char *desc;
    PropertyType_e type;
    void *userArg;
    union {
        struct {
            PropertySimpleAccessor_t set;
            PropertySimpleAccessor_t get;
        }simple;
    }accessors;
    struct PropertyNode_s *childNodes;
}PropertyNode_t;


/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static PropertyNode_t *PropertiesCreateNodes(const char *path);
static PropertyNode_t *PropertiesCreateNode(PropertyNode_t *parentNode, const char *newProp);
static PropertyNode_t *PropertiesFindNode(PropertyPathElements_t *pathElements, int *leftOver, PropertyNode_t **before);
static int PropertiesStrToValue(char *input, PropertyType_e toType, PropertyValue_t *output);
static PropertyPathElements_t * PropertyPathSplitElements(const char *path);
static void PropertyPathElementsDestructor(void * ptr);
static void PropertryDestructor(void *ptr);
/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static PropertyNode_t rootProperty;
static char PROPERTIES[]="Properties";

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/
int PropertiesInit(void)
{
    rootProperty.parent = NULL;
    rootProperty.next = NULL;
    rootProperty.name = "";
    rootProperty.desc = "Root of all properties";
    rootProperty.type = PropertyType_None;
    rootProperty.childNodes = NULL;
    ObjectRegisterTypeDestructor(PropertyNode_t, PropertryDestructor);
    ObjectRegisterTypeDestructor(PropertyPathElements_t, PropertyPathElementsDestructor);
    return 0;
}

int PropertiesDeInit(void)
{
    /* Run down the tree and free all Property Nodes and lists. */
    while (rootProperty.childNodes)
    {
        PropertiesRemoveAllProperties(rootProperty.childNodes->name);
    }
    
    return 0;
}

int PropertiesAddProperty(const char *path, const char *name, const char *desc, PropertyType_e type, 
                              void *userArg, PropertySimpleAccessor_t get, PropertySimpleAccessor_t set)
{
    PropertyNode_t *parentNode = PropertiesCreateNodes(path);
    PropertyNode_t *propertyNode = PropertiesCreateNode(parentNode, name);
    
    propertyNode->desc =desc;
    propertyNode->type = type;
    propertyNode->userArg = userArg;

    propertyNode->accessors.simple.get = get;
    propertyNode->accessors.simple.set = set;
    return 0;
}

int PropertiesRemoveProperty(const char *path, const char *name)
{
    int result = 0;
    int leftOver;
    PropertyPathElements_t *pathElements = PropertyPathSplitElements(path);
    PropertyNode_t *parentNode = PropertiesFindNode(pathElements, &leftOver, NULL);
    ObjectRefDec(pathElements);
    
    if ((parentNode == NULL) || (leftOver != -1))
    {
        LogModule(LOG_ERROR, PROPERTIES, "Couldn't find parent \"%s\" while trying to remove node %s", path, name);
        result = -1;
    }
    else
    {
        PropertyNode_t *prevNode = NULL;
        PropertyNode_t *currentNode;
        for (currentNode = parentNode->childNodes; currentNode; currentNode = currentNode->next)
        {
            if (strcmp(name, currentNode->name) == 0)
            {
                break;
            }
            prevNode = currentNode;
        }
        if (currentNode == NULL)
        {
            LogModule(LOG_ERROR, PROPERTIES, "Couldn't find \"%s\" with parent %s", name, path);
            result = -1;
        }
        else
        {
            if (currentNode->childNodes != NULL)
            {
                LogModule(LOG_ERROR, PROPERTIES, "Not removing \"%s\" with parent %s as it has children", name, path);
                result = -1;
            }
            else
            {
                prevNode->next = currentNode->next;
                ObjectRefDec(currentNode);
            }
            
        }
    }
    return result;
}

int PropertiesRemoveAllProperties(const char *path)
{
    int result = 0;
    int leftOver;
    PropertyPathElements_t *pathElements = PropertyPathSplitElements(path);
    PropertyNode_t *prevNode = NULL;
    PropertyNode_t *node = PropertiesFindNode(pathElements, &leftOver, &prevNode);
    PropertyNode_t *nextNode;
    PropertyNode_t *parentNode = node;
    
    if ((node == NULL) || (leftOver != -1))
    {
        LogModule(LOG_ERROR, PROPERTIES, "Couldn't find parent \"%s\" while trying to remove nodes", path);
        result = -1;
    }
    else
    {
        do
        {
            for (;node->childNodes != NULL; node = node->childNodes);
            if (node != parentNode)
            {
                if (node->next == NULL)
                {
                    nextNode = node->parent;
                    nextNode->childNodes = NULL;
                }
                else
                {
                    nextNode = node->next;
                }
                ObjectRefDec(node);
                node = nextNode;
            }
        }while (node != parentNode);

        nextNode = parentNode->next;
        if (parentNode->parent->childNodes == parentNode)
        {
            parentNode->parent->childNodes = nextNode;
        }
        if (prevNode != NULL)
        {
            prevNode->next = nextNode;
        }
        ObjectRefDec(parentNode);        
    }
    ObjectRefDec(pathElements);
    return result;
}

int PropertiesSet(char *path, PropertyValue_t *value)
{
    int result = -1;
    int leftOver;
    PropertyPathElements_t *pathElements = PropertyPathSplitElements(path);
    PropertyNode_t *node = PropertiesFindNode(pathElements, &leftOver, NULL);

    if ((node != NULL) && (leftOver == -1))
    {
        if ((node->type == value->type) && (node->accessors.simple.set != NULL))
        {
            result = node->accessors.simple.set(node->userArg, value);
        }
        else
        {
            LogModule(LOG_ERROR, PROPERTIES, "Wrong type supplied as value while trying to set property %s!", path);
        }
    }
    ObjectRefDec(pathElements);
    return result;
}

int PropertiesGet(char *path, PropertyValue_t *value)
{
    int result = -1;
    int leftOver;
    PropertyPathElements_t *pathElements = PropertyPathSplitElements(path);
    PropertyNode_t *node = PropertiesFindNode(pathElements, &leftOver, NULL);
    
    if ((node != NULL) && (leftOver == -1))
    {
        if (node->accessors.simple.get != NULL)
        {
            value->type = node->type;
            result = node->accessors.simple.get(node->userArg, value);
        }
    }
    ObjectRefDec(pathElements);
    return result;
}

int PropertiesSetStr(char *path, char *value)
{
    int result = -1;
    PropertyValue_t newValue;
    int leftOver;
    PropertyPathElements_t *pathElements = PropertyPathSplitElements(path);
    PropertyNode_t *node = PropertiesFindNode(pathElements, &leftOver, NULL);

    if ((node == NULL) || (leftOver != -1))
    {
        result = -1;
    }
    else
    {
        PropertiesStrToValue(value, node->type, &newValue);

        if ((node->type == newValue.type) && (node->accessors.simple.set != NULL))
        {
            result = node->accessors.simple.set(node->userArg, &newValue);
        }
        else
        {
            LogModule(LOG_ERROR, PROPERTIES, "Wrong type supplied as value while trying to set property %s!", path);
            result = -1;
        }

    } 
    ObjectRefDec(pathElements);    
    return result;
}

int PropertiesEnumerate(char *path, PropertiesEnumerator_t *pos)
{
    int result = 0;
    int leftOver = -1;
    PropertyPathElements_t *pathElements = NULL;
    PropertyNode_t *node = NULL;
    if ((path == NULL) || (path[0] == 0))
    {
        node = &rootProperty;
    }
    else
    {
        pathElements = PropertyPathSplitElements(path);
        node = PropertiesFindNode(pathElements, &leftOver, NULL);
    }
    if ((node == NULL) || (leftOver != -1))
    {
        result = -1;
    }
    else
    {
        *pos = node->childNodes;
    }
    
    if (pathElements)
    {
        ObjectRefDec(pathElements);
    }
    
    return result;
}

PropertiesEnumerator_t PropertiesEnumNext(PropertiesEnumerator_t pos)
{
    PropertyNode_t *node = pos;
    if (node)
    {
        return node->next;
    }
    return NULL;
}

void PropertiesEnumGetInfo(PropertiesEnumerator_t pos, PropertyInfo_t *propInfo)
{
    PropertyNode_t *node = pos;
    
    if (node)
    {
        propInfo->name = (char *)node->name;
        propInfo->desc = (char *)node->desc;
        propInfo->type = node->type;
        propInfo->readable = (node->accessors.simple.get != NULL);
        propInfo->writeable = (node->accessors.simple.set != NULL);       
        propInfo->hasChildren = (node->childNodes != NULL);
        
    }
}

int PropertiesGetInfo(char *path, PropertyInfo_t *propInfo)
{
    int result = 0;
    int leftOver;
    PropertyPathElements_t *pathElements = PropertyPathSplitElements(path);
    PropertyNode_t *node = PropertiesFindNode(pathElements, &leftOver, NULL);
    
    if ((node == NULL) || (leftOver != -1))
    {
        result = -1;
    }
    else
    {
        propInfo->name = (char *)node->name;
        propInfo->desc = (char *)node->desc;
        propInfo->type = node->type;
        propInfo->readable = (node->accessors.simple.get != NULL);
        propInfo->writeable = (node->accessors.simple.set != NULL);       
        propInfo->hasChildren = (node->childNodes != NULL);
    }
    ObjectRefDec(pathElements);
    return result;
}

static int PropertiesStrToValue(char *input, PropertyType_e toType, PropertyValue_t *output)
{
    unsigned int pid;
    
    output->type = PropertyType_None;
    switch(toType)
    {
        case PropertyType_Int:
            if (sscanf(input, "%d", &output->u.integer) == 1)
            {
                output->type = PropertyType_Int;
            }
            break;
            
        case PropertyType_Float:
            if (sscanf(input, "%lf", &output->u.fp) == 1)
            {
                output->type = PropertyType_Float;
            }
            break;
            
        case PropertyType_Boolean:
            if (strcasecmp(input, "true") == 0)
            {
                output->type = PropertyType_Boolean;
                output->u.boolean = TRUE;                        
            }
            else if (strcasecmp(input, "false") == 0)
            {
                output->type = PropertyType_Boolean;
                output->u.boolean = FALSE;
            }
            break;

        case PropertyType_String:
            output->type = PropertyType_String;
            output->u.string = strdup(input);
            break;

        case PropertyType_Char:
            output->type = PropertyType_Char;
            output->u.ch = *input;
            break;
            
        case PropertyType_PID:
            if (sscanf(input, "%u", &pid) == 0)
            {
                if (pid < 8193)
                {
                    output->type = PropertyType_PID;
                    output->u.pid = (uint16_t)pid;
                }
            }
            break;
            
        case PropertyType_IPAddress:
            
            break;
            
        default:
            break;
    }
    
    return output->type == PropertyType_None ? -1:0;
}

int PropertiesSimplePropertyGet(void *userArg, PropertyValue_t *value)
{
    int result = 0;
    switch(value->type)
    {
        case PropertyType_Int:
            value->u.integer = *(int *)userArg;
            break;
            
        case PropertyType_Float:
            value->u.fp = *(double *)userArg;
            break;
            
        case PropertyType_Boolean:
            value->u.boolean = *(bool*)userArg;
            break;

        case PropertyType_String:
            value->u.string = strdup(*(char**)userArg);
            break;

        case PropertyType_Char:
            value->u.ch = *(char*)userArg;
            break;
            
        case PropertyType_PID:
            value->u.pid = *(uint16_t*)userArg;
            break;
            
        case PropertyType_IPAddress:
            result = -1;
            break;
            
        default:
            result = -1;
            break;
    }
    return  result;
}

int PropertiesSimplePropertySet(void *userArg, PropertyValue_t *value)
{
    int result = 0;
    switch(value->type)
    {
        case PropertyType_Int:
            *(int *)userArg = value->u.integer;
            break;
            
        case PropertyType_Float:
            *(double *)userArg = value->u.fp;
            break;
            
        case PropertyType_Boolean:
            *(bool*)userArg = value->u.boolean;
            break;

        case PropertyType_String:
            *(char**)userArg = value->u.string;
            break;

        case PropertyType_Char:
            *(char*)userArg = value->u.ch;
            break;
            
        case PropertyType_PID:
            *(uint16_t*)userArg = value->u.pid;
            break;
            
        case PropertyType_IPAddress:
            result = -1;
            break;
            
        default:
            result = -1;
            break;
    }
    return  result;
}

static PropertyNode_t *PropertiesCreateNodes(const char *path)
{
    int toCreate = -1;
    int i;
    PropertyNode_t *currentNode;
    PropertyPathElements_t *pathElements = PropertyPathSplitElements(path);
    
    currentNode = PropertiesFindNode(pathElements, &toCreate, NULL);

    if (currentNode == NULL)
    {
        currentNode = &rootProperty;
    }
    if (toCreate != -1)
    {
        for (i = toCreate; i < pathElements->nrofElements; i ++)
        {
            currentNode = PropertiesCreateNode(currentNode, pathElements->elements[i]);
        }
    }
    ObjectRefDec(pathElements);
    return currentNode;
}

static PropertyNode_t *PropertiesCreateNode(PropertyNode_t *parentNode, const char *newProp)
{
    PropertyNode_t *childNode = NULL;

    childNode = ObjectCreateType(PropertyNode_t);
    childNode->name = strdup(newProp);
    childNode->type = PropertyType_None;
    childNode->desc = NULL;
    childNode->parent = parentNode;
    if (parentNode->childNodes)
    {
        PropertyNode_t *node, *prevNode = NULL;
        for (node = parentNode->childNodes; node; node = node->next)
        {
            if (strcmp(node->name, newProp) > 0)
            {
                break;
            }
            prevNode = node;
        }
        if (node == NULL)
        {
            prevNode->next = childNode;
            childNode->next = NULL;
        }
        else
        {
            childNode->next = node;
            if (prevNode)
            {
                prevNode->next = childNode;
            }
            else
            {
                parentNode->childNodes = childNode;
            }
        }
    }
    else
    {
        parentNode->childNodes = childNode;
    }
    return childNode;
}

static PropertyNode_t *PropertiesFindNode(PropertyPathElements_t *pathElements, int *leftOver, PropertyNode_t **before)
{
    PropertyNode_t *result = NULL;
    PropertyNode_t *currentNode = &rootProperty;
    PropertyNode_t *prevNode = NULL;
    PropertyNode_t *childNode;
    int i;
    bool nodeFound = TRUE;
    *leftOver = -1;
    for (i = 0; (i < pathElements->nrofElements); i ++)
    {
        nodeFound = FALSE;
        prevNode = NULL;
        for (childNode = currentNode->childNodes; childNode; childNode = childNode->next)
        {
            if (strcmp(childNode->name, pathElements->elements[i]) == 0)
            {
                currentNode = childNode;
                nodeFound = TRUE;
                break;
            }
            prevNode = childNode;
        }
        if (!nodeFound)
        {
            break;
        }
    }
    if (before != NULL)
    {
        *before = prevNode;
    }
    
    if (i < pathElements->nrofElements)
    {
        *leftOver = i;    
    }
    
    if (currentNode != &rootProperty)
    {
        result = currentNode;
    }
    return result;
}

static PropertyPathElements_t * PropertyPathSplitElements(const char *path)
{
    PropertyPathElements_t *pathElements = ObjectCreateType(PropertyPathElements_t);
    char *elementStart = (char *)path;  
    char *elementEnd = NULL;
    char nodeName[PROPERTIES_PATH_MAX];
    while(elementStart != NULL)
    {
        elementEnd = strchr(elementStart, '.');
        if (elementEnd)
        {
            int len = elementEnd - elementStart;
            strncpy(nodeName, elementStart, len);
            nodeName[len] = 0;
            elementStart = elementEnd + 1;
        }
        else
        {
            strcpy(nodeName, elementStart);
            elementStart = NULL;
        }
        pathElements->elements[pathElements->nrofElements] = strdup(nodeName);
        pathElements->nrofElements += 1;
    };
    return pathElements;
}

static void PropertyPathElementsDestructor(void *ptr)
{
    PropertyPathElements_t *pathElements = ptr;
    int i;
    for (i = 0; i < pathElements->nrofElements; i ++)
    {
        free(pathElements->elements[i]);
    }
}

static void PropertryDestructor(void *ptr)
{
    PropertyNode_t *node = (PropertyNode_t*)ptr;
    free((char *)node->name);
}
