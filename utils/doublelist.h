#ifndef ALSH_DOUBLE_LIST_
#define ALSH_DOUBLE_LIST_

#include <stdbool.h>

typedef struct DoubleList {
    double *data;
    int size;
    int capacity;
} DoubleList;

DoubleList* DoubleList_createCapacity(int capacity);
DoubleList* DoubleList_create(void);
void DoubleList_free(DoubleList *list);

void DoubleList_addAt(DoubleList *list, int index, double value);
void DoubleList_add(DoubleList *list, double value);
void DoubleList_clear(DoubleList *list);
double DoubleList_get(DoubleList *list, int index);
int DoubleList_indexOf(DoubleList *list, double value);
bool DoubleList_contains(DoubleList *list, double value);
double DoubleList_peek(DoubleList *list);
double DoubleList_removeIndex(DoubleList *list, int index);
void DoubleList_removeValue(DoubleList *list, double value);
double DoubleList_pop(DoubleList *list);

#endif
