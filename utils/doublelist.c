#include "doublelist.h"

#include "ealloc.h"
#include <stdlib.h>
#include <string.h>

#define DEFAULT_DOUBLELIST_CAPACITY 10
#define EPSILON 0.00001

void DoubleList_resize(DoubleList *list) {
    int oldCapacity = list->capacity;
    list->capacity *= 2;
    list->data = erealloc(list->data, sizeof(double) * (size_t) list->capacity);
    memset(list->data + oldCapacity, 0, (size_t) (list->capacity - oldCapacity));
}

double fabs(double d) {
    return d < 0 ? -d : d;
}

DoubleList* DoubleList_createCapacity(int capacity) {
    if (capacity <= 0) {
        return NULL;
    }
    DoubleList *list = emalloc(sizeof(DoubleList));
    list->data = ecalloc((size_t) capacity, sizeof(double));
    list->size = 0;
    list->capacity = capacity;
    return list;
}

DoubleList* DoubleList_create(void) {
    return DoubleList_createCapacity(DEFAULT_DOUBLELIST_CAPACITY);
}

void DoubleList_free(DoubleList *list) {
    free(list->data);
    free(list);
}

void DoubleList_addAt(DoubleList *list, int index, double value) {
    if (index < 0 || index > list->size) {
        return;
    }
    if (list->size + 1 == list->capacity) {
        DoubleList_resize(list);
    }
    for (int i = list->size; i > index; i--) {
        list->data[i] = list->data[i - 1];
    }
    list->data[index] = value;
    list->size++;
}

void DoubleList_add(DoubleList *list, double value) {
    DoubleList_addAt(list, list->size, value);
}

void DoubleList_clear(DoubleList *list) {
    for (int i = 0; i < list->size; i++) {
        list->data[i] = 0;
    }
    list->size = 0;
}

double DoubleList_get(DoubleList *list, int index) {
    if (index < 0 || index >= list->size) {
        return 0;
    }
    return list->data[index];
}

int DoubleList_indexOf(DoubleList *list, double value) {
    for (int i = 0; i < list->size; i++) {
        if (fabs(list->data[i] - value) < EPSILON) {
            return i;
        }
    }
    return -1;
}

bool DoubleList_contains(DoubleList *list, double value) {
    return DoubleList_indexOf(list, value) >= 0;
}

double DoubleList_peek(DoubleList *list) {
    return DoubleList_get(list, list->size - 1);
}

double DoubleList_removeIndex(DoubleList *list, int index) {
    if (index < 0 || index >= list->size) {
        return 0;
    }
    double value = list->data[index];
    int i;
    for (i = index; i < list->size - 1; i++) {
        list->data[i] = list->data[i + 1];
    }
    list->data[i] = 0;
    list->size--;
    return value;
}

void DoubleList_removeValue(DoubleList *list, double value) {
    int index = DoubleList_indexOf(list, value);
    if (index != -1) {
        DoubleList_removeIndex(list, index);
    }
}

double DoubleList_pop(DoubleList *list) {
    return DoubleList_removeIndex(list, list->size - 1);
}
