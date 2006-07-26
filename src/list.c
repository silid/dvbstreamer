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
#include <stdlib.h>
#include "list.h"

List_t *ListCreate()
{
    return calloc(1, sizeof(List_t));
}

void ListFree(List_t *list)
{
    ListEntry_t *entry;
    ListEntry_t *next;
    for (entry = list->head; entry != NULL; entry = next)
    {
        next = entry->next;
        free(entry);
    }
    free(list);
}

bool ListAdd(List_t *list, void *data)
{
    ListEntry_t *entry = calloc(1, sizeof(ListEntry_t));
    if (entry == NULL)
    {
        return FALSE;
    }
    entry->data = data;
    entry->prev = list->tail;
    list->tail = entry;
    if (list->head == NULL)
    {
        list->head = entry;
    }
    return TRUE;
}

bool ListInsertAfterCurrent(ListIterator_t *iterator, void *data)
{
    ListEntry_t *entry;

    entry = calloc(1, sizeof(ListEntry_t));
    if (entry == NULL)
    {
        return FALSE;
    }
    entry->data = data;
    entry->prev = iterator->current;
    if (iterator->current)
    {
        entry->next = iterator->current->next;
        iterator->current->next = entry;
    }

    if (iterator->current == iterator->list->head)
    {
        iterator->list->head = entry;
    }

    if (iterator->current == iterator->list->tail)
    {
        iterator->list->tail = entry;
    }
    return TRUE;
}

bool ListInsertBeforeCurrent(ListIterator_t *iterator, void *data)
{
    ListEntry_t *entry;

    entry = calloc(1, sizeof(ListEntry_t));
    if (entry == NULL)
    {
        return FALSE;
    }
    entry->data = data;
    entry->next = iterator->current;
    if (iterator->current)
    {
        entry->prev = iterator->current->prev;
        iterator->current->prev = entry;
    }

    if (iterator->current == iterator->list->head)
    {
        iterator->list->head = entry;
    }

    if (iterator->current == iterator->list->tail)
    {
        iterator->list->tail = entry;
    }
    return TRUE;
}

bool ListRemove(List_t *list, void *data)
{
    ListIterator_t iterator;
    for ( ListIterator_Init(iterator, list); ListIterator_MoreEntries(iterator); ListIterator_Next(iterator))
    {
        if (data == ListIterator_Current(iterator))
        {
            ListRemoveCurrent(&iterator);
            return TRUE;
        }
    }
    return FALSE;
}

void ListRemoveCurrent(ListIterator_t *iterator)
{
    List_t *list = iterator->list;
    ListEntry_t *entry = iterator->current;
    iterator->current = entry->next;

    if (entry == list->head)
    {
        list->head = entry->next;
    }
    if (entry == list->tail)
    {
        list->tail = entry->prev;
    }
    if (entry->prev)
    {
        entry->prev->next = entry->next;
    }
    if (entry->next)
    {
        entry->next->prev = entry->prev;
    }
}
