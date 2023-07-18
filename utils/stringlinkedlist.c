#include "stringlinkedlist.h"
#include <stdlib.h>
#include <string.h>

StringLinkedList* StringLinkedList_create() {
    StringLinkedList *list = malloc(sizeof(StringNode));
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

void StringLinkedList_append(StringLinkedList *list, char *str, bool strMustBeFreed) {
    if (list->head == NULL) {
        list->head = malloc(sizeof(StringNode));
        list->head->str = str;
        list->head->strMustBeFreed = strMustBeFreed;
        list->head->next = NULL;
        list->tail = list->head;
    } else {
        list->tail->next = malloc(sizeof(StringNode));
        list->tail->next->str = str;
        list->tail->next->strMustBeFreed = strMustBeFreed;
        list->tail->next->next = NULL;
        list->tail = list->tail->next;
    }
    list->size++;
}

char* StringLinkedList_get(StringLinkedList *list, int index) {
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
    return StringLinkedList_indexOf(list, str) != -1;
}

void StringLinkedList_prepend(StringLinkedList *list, char *str, bool strMustBeFreed) {
    StringNode *newHead = malloc(sizeof(StringNode));
    newHead->str = str;
    newHead->strMustBeFreed = strMustBeFreed;
    newHead->next = list->head;
    list->head = newHead;
    if (list->tail == NULL) {
        list->tail = list->head;
    }
    list->size++;
}

char* StringLinkedList_removeIndex(StringLinkedList *list, int index) {
    char *str = NULL;
    if (index == 0) {
        StringNode *temp = list->head;
        list->head = list->head->next;
        str = temp->str;
        free(temp);
        list->size--;
    } else {
        StringNode *temp = list->head;
        StringNode *prev = NULL;
        int i = 0;
        while (temp != NULL) {
            if (i++ == index) {
                if (temp->next == NULL) {
                    list->tail = prev;
                }
                prev->next = temp->next;
                str = temp->str;
                free(temp);
                list->size--;
                break;
            }
            prev = temp;
            temp = temp->next;
        }
    }
    return str;
}

void StringLinkedList_removeValue(StringLinkedList *list, char *str) {
    if (strcmp(list->head->str, str) == 0) {
        (void) StringLinkedList_removeIndex(list, 0);
    } else {
        StringNode *temp = list->head;
        StringNode *prev = NULL;
        while (temp != NULL) {
            if (strcmp(temp->str, str) == 0) {
                if (temp->next == NULL) {
                    list->tail = prev;
                }
                prev->next = temp->next;
                free(temp);
                list->size--;
                break;
            }
            prev = temp;
            temp = temp->next;
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
    char **array = malloc(sizeof(char*) * list->size);
    int i = 0;
    for (StringNode *temp = list->head; temp != NULL; temp = temp->next) {
        array[i++] = temp->str;
    }
    return array;
}
