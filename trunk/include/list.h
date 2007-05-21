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

list.h

Generic list management functions.

*/

#ifndef _DVBSTREAMER_LIST_H
#define _DVBSTREAMER_LIST_H

#include "types.h"

/**
 * @defgroup List Generic Linked List functions
 *@{
 */

typedef struct ListEntry_t
{
    void *data;
    struct ListEntry_t *next, *prev;
}ListEntry_t;

typedef struct List_t
{
    int count;
    ListEntry_t *head;
    ListEntry_t *tail;
}List_t;

typedef struct sListIterator_t
{
    List_t *list;
    ListEntry_t *current;
}ListIterator_t;

/**
 * Function pointer to a function to call with each entry->data in a list when 
 * the list is being freed.
 */
typedef void (*ListDataDestructor_t)(void *);

/**
 * Initialise a ListIterator_t instance to point to the first entry in the list.
 * Example use of a ListIterator_t
 * @code
 * ListIterator_t iterator;
 * for ( ListIterator_Init(iterator, list); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
 * {
 *    printf("Data = %p\n", ListIterator_Current(iterator));
 * }
 * @endcode
 *
 * @param _iterator The iterator to initialise (not a pointer),
 * @param _list The list to initialise the iterator to use.
 */
#define ListIterator_Init(_iterator, _list) (_iterator).current = (_list)->head, (_iterator).list = _list
/**
 * Move to the next entry.
 * @param _iterator Iterator to update.
 */
#define ListIterator_Next(_iterator)        ((_iterator).current = ((_iterator).current?((_iterator).current->next):NULL))
/**
 * Retrieve the data stored at current entry.
 * @param _iterator The iterator to retrieve the data from.
 * @return The data stored at the current entry.
 */
#define ListIterator_Current(_iterator)     ((_iterator).current->data)

/**
 * Determine whether there are any more entries in the list.
 * @param iterator The iterator to test.
 * @return True if there are more entries, false otherwise.
 */
#define ListIterator_MoreEntries(_iterator) ((_iterator).current)

/**
 * Creates a new double linked list.
 *@return A new List_t instance or NULL if there is not enough memory.
 */
List_t *ListCreate();

/**
 * Free a list and all the entries calling the destructor function for each entry.
 * @param list The list to free.
 */
void ListFree(List_t *list, void (*destructor)(void *data));

/**
 * Add an entry to the end of the list.
 * @param list The list to add to.
 * @param data The data entry to add.
 * @return TRUE if the data was added, false otherwise.
 */
bool ListAdd(List_t *list, void *data);

/**
 * Remove the first instance of data from the list.
 * @param list The list to remove the data from.
 * @param data The data to remove.
 * @return true if the data was found.
 */
bool ListRemove(List_t *list, void *data);

/**
 * Insert an entry before the current entry in a list.
 * @param iterator Current point in the list to insert the entry.
 * @param data The data to insert.
 * @return TRUE if the data was added, false otherwise.
 */
bool ListInsertBeforeCurrent(ListIterator_t *iterator, void *data);
/**
 * Insert an entry after the current entry in a list.
 * @param iterator Current point in the list to insert the entry.
 * @param data The data to insert.
 * @return TRUE if the data was added, false otherwise.
 */
bool ListInsertAfterCurrent(ListIterator_t *iterator, void *data);
/**
 * Remove the current entry in the list pointed to by the iterator.
 * After this function complete the iterator will point to the next entry in
 * the list.
 * @param iterator Iterator pointer to the current entry in the list to remove.
 */
void ListRemoveCurrent(ListIterator_t *iterator);

/**
 * Returns the number of entries in the specified list.
 * @param _list The list to retrieve the number of entries from.
 * @return The number of entries in the list.
 */
#define ListCount(_list) (_list)->count

/**
 * Dump the structure of a list.
 * @param list the list to dump.
 */
void ListDump(List_t *list);

/** @} */
#endif
