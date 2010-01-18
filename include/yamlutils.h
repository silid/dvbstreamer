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

yamlutils.h

Misc functions for processing YAML.

*/

#ifndef _YAMLUTILS_H
#define _YAMLUTILS_H
#include "yaml.h"

/**
 * Parses the supplied string into the supplied doucment.
 * @param str The string to parse.
 * @param document The document object to store the parsed yaml in.
 * @return 1 on success, 0 on error.
 */
int YamlUtils_Parse(char *str, yaml_document_t *document);

/**
 * Find the specified key in the supplied mapping node.
 * @param document The YAML document the mapping node belongs to.
 * @param node The mapping node to search.
 * @param key The key to find in the mappng node.
 * @return A yaml_node_t pointer pointing to the value node if the key is found, or NULL.
 */
yaml_node_t *YamlUtils_MappingFind(yaml_document_t *document, yaml_node_t *node, const char *key);

/**
 * Helper macro to retrieve a key from a root mapping node.
 * @note The _document parameter must not produce side-effects as it is used more than once.
 */
#define YamlUtils_RootMappingFind(_document, _key) \
    YamlUtils_MappingFind(_document, yaml_document_get_root_node(_document), _key)

/**
 * Helper function to add 2 scalar nodes (key, value) and then append these to a mapping.
 * @param document The document to add to.
 * @param mapping The mapping node id to add to.
 * @param key The key string to add to the mapping.
 * @param value The string value the key should map to.
 * @return 1 on success, 0 on error.
 */
int YamlUtils_MappingAdd(yaml_document_t *document, int mapping, const char *key, const char *value);

/**
 * Convert the supplied document into a string, destorying the document in the process.
 * @param document The document to convert.
 * @param removeDocStartEnd Whether to include document start and end markers.
 * @param outputStr Where to store the string created after converting the document.
 * @return The number of bytes written to the string.
 */
int YamlUtils_DocumentToString(yaml_document_t *document, bool removeDocStartEnd, char **outputStr);
#endif

