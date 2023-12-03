#ifndef ALSH_CHAR_LIST_
#define ALSH_CHAR_LIST_

#include <stdbool.h>

typedef struct CharList {
    char *data;
    int size;
    int capacity;
} CharList;

CharList* CharList_createCapacity(int capacity);
CharList* CharList_create(void);
void CharList_free(CharList *list);

void CharList_addAt(CharList *list, int index, char value);
void CharList_add(CharList *list, char value);
void CharList_addStr(CharList *list, char *str);
void CharList_clear(CharList *list);
char CharList_get(CharList *list, int index);
int CharList_indexOf(CharList *list, char value);
bool CharList_contains(CharList *list, char value);
char CharList_peek(CharList *list);
char CharList_removeIndex(CharList *list, int index);
void CharList_removeValue(CharList *list, char value);
char CharList_pop(CharList *list);
char* CharList_toStr(CharList *list);

#endif // ALSH_CHAR_LIST_
