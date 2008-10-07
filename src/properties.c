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
    struct PropertyNode_s *prev;
    
    char *name;
    char *desc;
    PropertyType_e type;
    void *userArg;
    union {
        struct {
            PropertySimpleAccessor_t set;
            PropertySimpleAccessor_t get;
        }simple;
        struct {
            PropertyTableDescription_t *tableDesc;
            PropertyTableAccessor_t set;
            PropertyTableAccessor_t get;
            PropertyTableCounter_t  count;
        }table;
    }accessors;
    struct PropertyNode_s *childNodes;
}PropertyNode_t;


/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static PropertyNode_t *PropertiesCreateNodes(char *path);
static PropertyNode_t *PropertiesCreateNode(PropertyNode_t *parentNode, char *newProp);
static PropertyNode_t *PropertiesFindNode(char *path, char **leftOver);
static int PropertiesStrToValue(char *input, PropertyType_e toType, PropertyValue_t *output);
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
    rootProperty.prev = NULL;
    rootProperty.name = "";
    rootProperty.desc = "Root of all properties";
    rootProperty.type = PropertyType_None;
    rootProperty.childNodes = NULL;
    ObjectRegisterTypeDestructor(PropertyNode_t, PropertryDestructor);
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

int PropertiesAddProperty(char *path, char *name, char *desc, PropertyType_e type, 
                              void *userArg, PropertySimpleAccessor_t get, PropertySimpleAccessor_t set)
{
    PropertyNode_t *parentNode = PropertiesCreateNodes(path);
    PropertyNode_t *propertyNode = PropertiesCreateNode(parentNode, name);
    
    propertyNode->desc = desc;
    propertyNode->type = type;
    propertyNode->userArg = userArg;

    propertyNode->accessors.simple.get = get;
    propertyNode->accessors.simple.set = set;
    return 0;
}

int PropertyAddTableProperty(char *path, char *name, char *desc, PropertyTableDescription_t *tableDesc,
                              void *userArg, PropertyTableAccessor_t get, PropertyTableAccessor_t set, 
                              PropertyTableCounter_t count)
{
    PropertyNode_t *parentNode = PropertiesCreateNodes(path);
    PropertyNode_t *propertyNode = PropertiesCreateNode(parentNode, name);
    
    propertyNode->desc = desc;
    propertyNode->type = PropertyType_Table;
    propertyNode->userArg = userArg;
    propertyNode->accessors.table.tableDesc = tableDesc;
    propertyNode->accessors.table.get = get;
    propertyNode->accessors.table.set = set;
    propertyNode->accessors.table.count = count;
    return 0;

}

int PropertiesRemoveProperty(char *path, char *name)
{
    int result = 0;
    char *leftOver;
    PropertyNode_t *parentNode = PropertiesFindNode(path, &leftOver);
    if ((path == NULL) || (leftOver != NULL))
    {
        LogModule(LOG_ERROR, PROPERTIES, "Couldn't find parent %s while trying to remove node %s", path, name);
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
            LogModule(LOG_ERROR, PROPERTIES, "Couldn't find %s with parent %s", name, path);
            result = -1;
        }
        else
        {
            if (currentNode->childNodes != NULL)
            {
                LogModule(LOG_ERROR, PROPERTIES, "Not removing %s with parent %s as it has children", name, path);
                result = -1;
            }
            else
            {
                prevNode->next = currentNode->next;
                currentNode->next->prev = prevNode;
                ObjectRefDec(currentNode);
            }
            
        }
    }
    return result;
}

int PropertiesRemoveAllProperties(char *path)
{
    int result = 0;
    char *leftOver;
    PropertyNode_t *node = PropertiesFindNode(path, &leftOver);
    PropertyNode_t *nextNode;
    PropertyNode_t *parentNode = node;
    
    if ((node == NULL) || (leftOver != NULL))
    {
        LogModule(LOG_ERROR, PROPERTIES, "Couldn't find parent %s while trying to remove nodes", path);
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
        if (nextNode != NULL)
        {
            nextNode->prev = parentNode->prev;
        }
        if (parentNode->prev != NULL)
        {
            parentNode->prev->next = nextNode;
        }
        else
        {
            parentNode->parent->childNodes = nextNode;
        }
        ObjectRefDec(parentNode);        
    }
    return result;
}

int PropertiesSet(char *path, PropertyValue_t *value)
{
    int result = -1;
    char *leftOver;
    PropertyNode_t *node = PropertiesFindNode(path, &leftOver);

    if ((node != NULL) && ((leftOver == NULL) || (node->type == PropertyType_Table)))
    {
        if (node->type == PropertyType_Table)
        {
            int row, column;
            /* Check that all dimensions are specified */
            if (sscanf(leftOver, ".%d.%d", &row, &column) == 2)
            {
                PropertyTableDescription_t *tableDesc = node->accessors.table.tableDesc;
                if (column < tableDesc->nrofColumns)
                {
                    if ((tableDesc->columns[column].type == value->type) &&
                        (node->accessors.table.set != NULL))
                    {
                        result = node->accessors.table.set(node->userArg, row, column, value);
                    }
                }
            }
        }
        else
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
    }
    return result;
}

int PropertiesGet(char *path, PropertyValue_t *value)
{
    int result = 0;
    char *leftOver;
    PropertyNode_t *node = PropertiesFindNode(path, &leftOver);
    if ((node == NULL) || ((leftOver != NULL) && (node->type != PropertyType_Table)))
    {
        result = -1;
    }
    else
    {
        if (node->type == PropertyType_Table)
        {
            /* Check that all dimensions are specified and call get */
            /* Otherwise call count */
        }
        else
        {
            if (node->accessors.simple.get != NULL)
            {
                value->type = node->type;
                result = node->accessors.simple.get(node->userArg, value);
            }
            else
            {
                result = -1;
            }
        }
    }
    return result;
}

int PropertiesSetStr(char *path, char *value)
{
    int result = -1;
    char *leftOver;
    PropertyValue_t newValue;
    PropertyNode_t *node = PropertiesFindNode(path, &leftOver);

    if ((node == NULL) || ((leftOver != NULL) && (node->type != PropertyType_Table)))
    {
        result = -1;
    }
    else
    {
        if (node->type == PropertyType_Table)
        {
            /* Check that all dimensions are specified */
        }
        else
        {
            PropertiesStrToValue(value, node->type, &newValue);

            if (node->type == newValue.type)
            {
                result = node->accessors.simple.set(node->userArg, &newValue);
            }
            else
            {
                LogModule(LOG_ERROR, PROPERTIES, "Wrong type supplied as value while trying to set property %s!", path);
                result = -1;
            }
        }
    } 
    return result;
}

int PropertiesEnumerate(char *path, PropertiesEnumerator_t *pos)
{
    int result = 0;
    char *leftOver = NULL;
    PropertyNode_t *node = NULL;
    if ((path == NULL) || (path[0] == 0))
    {
        node = &rootProperty;
    }
    else
    {
        node = PropertiesFindNode(path, &leftOver);
    }

    if ((node == NULL) || (leftOver != NULL))
    {
        result = -1;
    }
    else
    {
        *pos = node->childNodes;
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

void PropertiesEnumGetInfo(PropertiesEnumerator_t pos, char **name, char **desc, PropertyType_e *type, bool *get, bool *set, bool *branch)
{
    PropertyNode_t *node = pos;
    
    if (node)
    {
        *name = node->name;
        *desc = node->desc;
        *type = node->type;
        if (node->type == PropertyType_Table)
        {
            *get = (node->accessors.table.get != NULL);
        }
        else
        {
            *get = (node->accessors.simple.get != NULL);
        }
        if (node->type == PropertyType_Table)
        {
            *set = (node->accessors.table.set != NULL);
        }
        else
        {
            *set = (node->accessors.simple.set != NULL);
        }
        
        *branch = (node->childNodes != NULL);
    }
}

int PropertiesGetInfo(char *path, char **desc, PropertyType_e *type, bool *get, bool *set, bool *branch)
{
    int result = 0;
    char *leftOver;
    PropertyNode_t *node = PropertiesFindNode(path, &leftOver);
    if ((node == NULL) || (leftOver != NULL))
    {
        result = -1;
    }
    else
    {
        *desc = node->desc;
        *type = node->type;
        if (node->type == PropertyType_Table)
        {
            *get = (node->accessors.table.get != NULL);
        }
        else
        {
            *get = (node->accessors.simple.get != NULL);
        }
        if (node->type == PropertyType_Table)
        {
            *set = (node->accessors.table.set != NULL);
        }
        else
        {
            *set = (node->accessors.simple.set != NULL);
        }
        
        *branch = (node->childNodes != NULL);
    }
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

static PropertyNode_t *PropertiesCreateNodes(char *path)
{
    char *toCreate = NULL;
    PropertyNode_t *currentNode;
    char *elementStart = NULL;
    char *elementEnd = NULL;
    char nodeName[PROPERTIES_PATH_MAX];

    currentNode = PropertiesFindNode(path, &toCreate);

    if (currentNode == NULL)
    {
        currentNode = &rootProperty;
    }
    if (toCreate != NULL)
    {
        for (elementStart = toCreate; elementStart != NULL; )
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
            currentNode = PropertiesCreateNode(currentNode, nodeName);
        }
    }
    return currentNode;
}

static PropertyNode_t *PropertiesCreateNode(PropertyNode_t *parentNode, char *newProp)
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
            childNode->prev = prevNode;            
        }
        else
        {
            childNode->next = node;
            node->prev = childNode;
            childNode->prev = prevNode;
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
        childNode->prev = NULL;
        childNode->next = NULL;
    }
    return childNode;
}

static PropertyNode_t *PropertiesFindNode(char *path, char **leftOver)
{
    PropertyNode_t *result = NULL;
    PropertyNode_t *currentNode = &rootProperty;
    PropertyNode_t *childNode;
    char *elementStart = path;
    char *elementEnd = NULL;
    char nodeName[PROPERTIES_PATH_MAX];

    bool nodeFound = FALSE;
    *leftOver = path;
    do
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
        nodeFound = FALSE;
        for (childNode = currentNode->childNodes; childNode; childNode = childNode->next)
        {
            if (strcmp(childNode->name, nodeName) == 0)
            {
                currentNode = childNode;
                nodeFound = TRUE;
                *leftOver = elementStart;
                break;
            }
        }
    }while(nodeFound && (elementStart != NULL));
    
    if (currentNode != &rootProperty)
    {
        result = currentNode;
    }


    return result;
}

static void PropertryDestructor(void *ptr)
{
    PropertyNode_t *node = (PropertyNode_t*)ptr;
    free(node->name);
}
