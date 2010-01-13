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
* Global variables                                                             *
*******************************************************************************/
char YAMLUTILS[] = "YamlUtils";

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

int YamlUtils_MappingAdd(yaml_document_t *document, int mapping, const char *key, const char *value, const char *valueTag)
{
    int keyId;
    int valueId;

    keyId = yaml_document_add_scalar(document, (yaml_char_t*)YAML_STR_TAG, (yaml_char_t*)key, strlen(key), YAML_ANY_SCALAR_STYLE);
    valueId = yaml_document_add_scalar(document, (yaml_char_t*)valueTag, (yaml_char_t*)value, strlen(value), YAML_ANY_SCALAR_STYLE); 
    return yaml_document_append_mapping_pair(document, mapping, keyId, valueId);
}

