#include "charlist.h"

#include "ealloc.h"
#include <stdlib.h>
#include <string.h>

#define DEFAULT_CAPACITY 10

void CharList_resize(CharList *list) {
    int oldCapacity = list->capacity;
    list->capacity *= 2;
    list->data = erealloc(list->data, sizeof(char) * (size_t) list->capacity);
    memset(list->data + oldCapacity, 0, (size_t) (list->capacity - oldCapacity));
}

CharList* CharList_createCapacity(int capacity) {
    if (capacity <= 0) {
        return NULL;
    }
    CharList *list = emalloc(sizeof(CharList));
    list->data = ecalloc((size_t) capacity, sizeof(char));
    list->size = 0;
    list->capacity = capacity;
    return list;
}

CharList* CharList_create(void) {
    return CharList_createCapacity(DEFAULT_CAPACITY);
}

void CharList_free(CharList *list) {
    free(list->data);
    free(list);
}

void CharList_addAt(CharList *list, int index, char value) {
    if (index < 0 || index > list->size) {
        return;
    }
    if (list->size == list->capacity) {
        CharList_resize(list);
    }
    for (int i = list->size; i > index; i--) {
        list->data[i] = list->data[i - 1];
    }
    list->data[index] = value;
    list->size++;
}

void CharList_add(CharList *list, char value) {
    CharList_addAt(list, list->size, value);
}

void CharList_addStr(CharList *list, char *str) {
    while (*str) {
        CharList_add(list, *str++);
    }
}

void CharList_clear(CharList *list) {
    for (int i = 0; i < list->size; i++) {
        list->data[i] = 0;
    }
    list->size = 0;
}

char CharList_get(CharList *list, int index) {
    if (index < 0 || index >= list->size) {
        return 0;
    }
    return list->data[index];
}

int CharList_indexOf(CharList *list, char value) {
    for (int i = 0; i < list->size; i++) {
        if (list->data[i] == value) {
            return i;
        }
    }
    return -1;
}

bool CharList_contains(CharList *list, char value) {
    return CharList_indexOf(list, value) >= 0;
}

char CharList_removeIndex(CharList *list, int index) {
    if (index < 0 || index >= list->size) {
        return 0;
    }
    char value = list->data[index];
    int i;
    for (i = index; i < list->size - 1; i++) {
        list->data[i] = list->data[i + 1];
    }
    list->data[i] = 0;
    list->size--;
    return value;
}

void CharList_removeValue(CharList *list, char value) {
    int index = CharList_indexOf(list, value);
    if (index != -1) {
        CharList_removeIndex(list, index);
    }
}

char* CharList_toStr(CharList *list) {
    char *arr = emalloc(sizeof(char) * (size_t) (list->size + 1));
    memcpy(arr, list->data, (size_t) list->size);
    arr[list->size] = 0;
    return arr;
}
