#include "stringlinkedlist.h"

#include "ealloc.h"
#include <stdlib.h>
#include <string.h>

StringLinkedList* StringLinkedList_create(void) {
    StringLinkedList *list = emalloc(sizeof(StringNode));
    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
    return list;
}

void StringLinkedList_free(StringLinkedList* list) {
    StringNode *temp = list->head;
    while (temp != NULL) {
        StringNode *next = temp->next;
        if (temp->strMustBeFreed) {
            free(temp->str);
        }
        free(temp);
        temp = next;
    }
    free(list);
}

void StringLinkedList_addAt(StringLinkedList *list, int index, char *str, bool strMustBeFreed) {
    if (index < 0 || index > list->size) {
        return;
    }
    if (index == 0) {
        StringNode *newNode = emalloc(sizeof(StringNode));
        newNode->str = str;
        newNode->strMustBeFreed = strMustBeFreed;
        newNode->next = list->head;
        list->head = newNode;
        if (list->tail == NULL) {
            list->tail = list->head;
        }
        list->size++;
    } else if (index == list->size) {
        StringNode *newNode = emalloc(sizeof(StringNode));
        list->tail->next = newNode;
        list->tail->next->str = str;
        list->tail->next->strMustBeFreed = strMustBeFreed;
        list->tail->next->next = NULL;
        list->tail = list->tail->next;
        list->size++;
    } else {
        int i = 0;
        for (StringNode *temp = list->head; temp != NULL; temp = temp->next) {
            if (i++ == index - 1) {
                StringNode *newNode = emalloc(sizeof(StringNode));
                newNode->str = str;
                newNode->strMustBeFreed = strMustBeFreed;
                newNode->next = temp->next;
                temp->next = newNode;
                list->size++;
                break;
            }
        }
    }
}

void StringLinkedList_append(StringLinkedList *list, char *str, bool strMustBeFreed) {
    StringLinkedList_addAt(list, list->size, str, strMustBeFreed);
}

char* StringLinkedList_get(StringLinkedList *list, int index) {
    if (index < 0 || index >= list->size) {
        return NULL;
    }
    if (index == list->size - 1) {
        return list->tail != NULL ? list->tail->str : NULL;
    }
    int i = 0;
    for (StringNode *temp = list->head; temp != NULL; temp = temp->next) {
        if (i++ == index) {
            return temp->str;
        }
    }
    return NULL;
}

int StringLinkedList_indexOf(StringLinkedList *list, char *str) {
    int i = 0;
    for (StringNode *temp = list->head; temp != NULL; temp = temp->next) {
        if (strcmp(temp->str, str) == 0) {
            return i;
        }
        i++;
    }
    return -1;
}

bool StringLinkedList_contains(StringLinkedList *list, char *str) {
    return StringLinkedList_indexOf(list, str) >= 0;
}

void StringLinkedList_prepend(StringLinkedList *list, char *str, bool strMustBeFreed) {
    StringLinkedList_addAt(list, 0, str, strMustBeFreed);
}

StringNode* StringLinkedList_removeIndex(StringLinkedList *list, int index) {
    if (index < 0 || index >= list->size) {
        return NULL;
    }
    StringNode *temp = list->head;
    if (index == 0) {
        if (list->head == list->tail) {
            list->tail = NULL;
        }
        list->head = list->head->next;
        list->size--;
    } else {
        StringNode *prev = NULL;
        int i = 0;
        for (; temp != NULL; temp = temp->next) {
            if (i++ == index) {
                if (temp->next == NULL) {
                    list->tail = prev;
                }
                prev->next = temp->next;
                list->size--;
                break;
            }
            prev = temp;
        }
    }
    if (temp != NULL) {
        temp->next = NULL;
    }
    return temp;
}

void StringLinkedList_removeIndexAndFreeNode(StringLinkedList *list, int index) {
    StringNode *node = StringLinkedList_removeIndex(list, index);
    if (node != NULL) {
        if (node->strMustBeFreed) {
            free(node->str);
        }
        free(node);
    }
}

void StringLinkedList_removeValue(StringLinkedList *list, char *str) {
    if (list->head == NULL) return;
    if (strcmp(list->head->str, str) == 0) {
        StringLinkedList_removeIndexAndFreeNode(list, 0);
    } else {
        StringNode *prev = list->head;
        for (StringNode *temp = list->head->next; temp != NULL; temp = temp->next) {
            if (strcmp(temp->str, str) == 0) {
                if (temp->next == NULL) {
                    list->tail = prev;
                }
                prev->next = temp->next;
                if (temp->strMustBeFreed) {
                    free(temp->str);
                }
                free(temp);
                list->size--;
                break;
            }
            prev = temp;
        }
    }
}

int StringLinkedList_size(StringLinkedList *list) {
    int size = 0;
    for (StringNode *temp = list->head; temp != NULL; temp = temp->next) {
        size++;
    }
    return size;
}

char** StringLinkedList_toArray(StringLinkedList *list) {
    char **array = emalloc(sizeof(char*) * (size_t) list->size);
    int i = 0;
    for (StringNode *temp = list->head; temp != NULL; temp = temp->next) {
        array[i++] = temp->str;
    }
    return array;
}
