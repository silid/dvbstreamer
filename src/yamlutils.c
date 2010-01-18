/*
Copyright (C) 2010  Adam Charrett

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

yamlutils.c

Misc functions for processing YAML.

*/

#include "logging.h"
#include "objects.h"
#include "yaml.h"

/*******************************************************************************
* Defines                                                                      *
*******************************************************************************/
#define STRING_SECTION_SIZE 256

/*******************************************************************************
* Typedefs                                                                     *
*******************************************************************************/
typedef struct YUStringSection_s{
    char buffer[STRING_SECTION_SIZE];
    struct YUStringSection_s *next;
}YUStringSection_t;

typedef struct StringOutput_s {
    size_t written;
    int currentSectionPos;
    YUStringSection_t *currentSection;
    YUStringSection_t *sections;
    YUStringSection_t *sectionsEnd;
}StringOutput_t;

/*******************************************************************************
* Prototypes                                                                   *
*******************************************************************************/
static int YamlUtils_OutputToStringSections(void *data, unsigned char* buffer, size_t size);
static void YamlUtils_AddStringSection(StringOutput_t *output);

/*******************************************************************************
* Global variables                                                             *
*******************************************************************************/
static char YAMLUTILS[] = "YamlUtils";

/*******************************************************************************
* Global functions                                                             *
*******************************************************************************/

int YamlUtils_Parse(char *str, yaml_document_t *document)
{    
    int r;
    yaml_parser_t parser;
    yaml_parser_initialize(&parser);
    yaml_parser_set_input_string(&parser, (const unsigned char*)str, strlen(str));
    
    r = yaml_parser_load(&parser, document);
    yaml_parser_delete(&parser);
    return r;
}

yaml_node_t *YamlUtils_MappingFind(yaml_document_t *document, yaml_node_t *node, const char *key)
{
    yaml_node_pair_t *pair;
    yaml_node_t *keyNode;
    if (node->type != YAML_MAPPING_NODE)
    {
        LogModule(LOG_ERROR,YAMLUTILS,"Node was not a mapping node!");
        return NULL;
    }
    for (pair = node->data.mapping.pairs.start; pair && (pair != node->data.mapping.pairs.top); pair ++)
    {
        keyNode = yaml_document_get_node(document, pair->key);
        if (keyNode)
        {
            if (strcmp((char*)keyNode->data.scalar.value, key) == 0)
            {
                return yaml_document_get_node(document, pair->value);
            }
        }
    }
    return NULL;
}

int YamlUtils_MappingAdd(yaml_document_t *document, int mapping, const char *key, const char *value)
{
    int keyId;
    int valueId;

    keyId = yaml_document_add_scalar(document, (yaml_char_t*)YAML_STR_TAG, (yaml_char_t*)key, strlen(key), YAML_ANY_SCALAR_STYLE);
    valueId = yaml_document_add_scalar(document, (yaml_char_t*)YAML_STR_TAG, (yaml_char_t*)value, strlen(value), YAML_ANY_SCALAR_STYLE); 
    return yaml_document_append_mapping_pair(document, mapping, keyId, valueId);
}



int YamlUtils_DocumentToString(yaml_document_t *document, bool removeDocStartEnd, char **outputStr)
{
    StringOutput_t output;
    char *current;
    YUStringSection_t *section;
    size_t left;
    yaml_emitter_t emitter;
    int offset = 0;
    size_t toCopy;

    ObjectRegisterType(YUStringSection_t);

    bzero(&output, sizeof(output));
    YamlUtils_AddStringSection(&output);
    
    yaml_emitter_initialize(&emitter);
    yaml_emitter_set_output(&emitter, YamlUtils_OutputToStringSections, &output);
    yaml_emitter_dump(&emitter, document);
    if (removeDocStartEnd)
    {
        output.written -= 8; /* Remove start and end document markers */
        offset = 4;
    }
    *outputStr = malloc(output.written + 1);
    current = *outputStr;
    section = output.sections;
    for (left = output.written; left && section; left -= toCopy)
    {
        toCopy = (STRING_SECTION_SIZE - offset);
        if (left < toCopy)
        {
            toCopy = left;
        }
        memcpy(current, section->buffer + offset, toCopy);
        offset = 0;
        current += toCopy;
        section = section->next;
    }
    *current = 0;
    for (section = output.sections; section;)
    {
        YUStringSection_t *prev = section;
        section = prev->next;
        ObjectRefDec(prev);
    }
    return output.written;
}


static int YamlUtils_OutputToStringSections(void *data, unsigned char* buffer, size_t size)
{
    StringOutput_t *output = data;
    int offset = 0;
    size_t left = size;
    while (left)
    {
        size_t available = STRING_SECTION_SIZE - output->currentSectionPos;
        if (left > available)
        {
            memcpy(output->currentSection->buffer + output->currentSectionPos, buffer + offset, available);            
            YamlUtils_AddStringSection(output);
            left -= available;
            offset += available;
        }
        else
        {
            memcpy(output->currentSection->buffer + output->currentSectionPos, buffer + offset, left);
            output->currentSectionPos += left;
            left = 0;
        }
    }
    output->written += size;
    return 1;
}

static void YamlUtils_AddStringSection(StringOutput_t *output)
{
    YUStringSection_t *result = ObjectCreateType(YUStringSection_t);
    if (output->sections == NULL)
    {
        output->sections = result;
    }
    else
    {
        output->sectionsEnd->next = result;
    }
    output->sectionsEnd = result;  
    output->currentSection = result;
    output->currentSectionPos = 0;
}
