#ifndef ALSH_STRING_LINKED_LIST_
#define ALSH_STRING_LINKED_LIST_

#include <stdbool.h>

typedef struct StringNode {
    char *str;
    bool strMustBeFreed;
    struct StringNode *next;
} StringNode;

typedef struct StringLinkedList {
    StringNode *head;
    StringNode *tail;
    int size;
} StringLinkedList;

StringLinkedList* StringLinkedList_create(void);
void StringLinkedList_free(StringLinkedList *list);

void StringLinkedList_addAt(StringLinkedList *list, int index, char *str, bool strMustBeFreed);
void StringLinkedList_append(StringLinkedList *list, char *str, bool strMustBeFreed);
char* StringLinkedList_get(StringLinkedList *list, int index);
int StringLinkedList_indexOf(StringLinkedList *list, char *str);
bool StringLinkedList_contains(StringLinkedList *list, char *str);
void StringLinkedList_prepend(StringLinkedList *list, char *str, bool strMustBeFreed);
StringNode* StringLinkedList_removeIndex(StringLinkedList *list, int index);
void StringLinkedList_removeIndexAndFreeNode(StringLinkedList *list, int index);
void StringLinkedList_removeValue(StringLinkedList *list, char *str);
int StringLinkedList_size(StringLinkedList *list);
char** StringLinkedList_toArray(StringLinkedList *list);

#endif // ALSH_STRING_LINKED_LIST_
