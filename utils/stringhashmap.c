#include "stringhashmap.h"

#include "ealloc.h"
#include <stdlib.h>
#include <string.h>

#define DEFAULT_SIZE 1000

typedef struct StringHashMapNode {
    char *key;
    bool keyMustBeFreed;
    char *value;
    bool valueMustBeFreed;
    struct StringHashMapNode *next;
} StringHashMapNode;

struct StringHashMap {
    StringHashMapNode **buckets;
    int bucketSize;
};

unsigned long hash(StringHashMap *map, char *str) {
    unsigned long hashVal = 5381;
    int c;
    while ((c = *str++)) {
        hashVal = ((hashVal << 5) + hashVal) + (unsigned long) c; //hash * 33 + c
    }
    return hashVal % (unsigned long) map->bucketSize;
}

StringHashMap* StringHashMap_createSize(int size) {
    if (size <= 0) {
        return NULL;
    }
    StringHashMap *map = emalloc(sizeof(StringHashMap));
    map->buckets = emalloc(sizeof(StringHashMapNode*) * (size_t) size);
    for (int i = 0; i < size; i++) {
        map->buckets[i] = NULL;
    }
    map->bucketSize = size;
    return map;
}

StringHashMap* StringHashMap_create(void) {
    return StringHashMap_createSize(DEFAULT_SIZE);
}

void StringHashMap_free(StringHashMap *map) {
    for (int i = 0; i < map->bucketSize; i++) {
        StringHashMapNode *temp = map->buckets[i];
        while (temp != NULL) {
            StringHashMapNode *next = temp->next;
            if (temp->keyMustBeFreed) {
                free(temp->key);
            }
            if (temp->valueMustBeFreed) {
                free(temp->value);
            }
            free(temp);
            temp = next;
        }
    }
    free(map->buckets);
    free(map);
}

char*** StringHashMap_entries(StringHashMap *map) {
    size_t entriesSize = 10;
    int entriesIndex = 0;
    char ***entries = ecalloc(entriesSize, sizeof(char**));
    for (int i = 0; i < map->bucketSize; i++) {
        for (StringHashMapNode *temp = map->buckets[i]; temp != NULL; temp = temp->next) {
            char **entry = emalloc(sizeof(char*) * 2);
            entry[0] = temp->key;
            entry[1] = temp->value;
            entries[entriesIndex++] = entry;
            if ((size_t) entriesIndex >= entriesSize - 1) {
                entriesSize *= 2;
                entries = erealloc(entries, sizeof(char**) * entriesSize);
            }
        }
    }
    return entries;
}

char* StringHashMap_get(StringHashMap *map, char *key) {
    unsigned long keyHash = hash(map, key);
    StringHashMapNode *mapNode = map->buckets[keyHash];
    if (mapNode == NULL) {
        return NULL;
    }
    if (mapNode->next == NULL) {
        return mapNode->value;
    }
    for (StringHashMapNode *temp = mapNode->next; temp != NULL; temp = temp->next) {
        if (strcmp(key, temp->key) == 0) {
            return temp->value;
        }
    }
    return NULL;
}

bool* StringHashMap_getMustBeFreed(StringHashMap *map, char *key) {
    unsigned long keyHash = hash(map, key);
    StringHashMapNode *mapNode = map->buckets[keyHash];
    if (mapNode == NULL) {
        return NULL;
    }
    if (mapNode->next == NULL) {
        bool *vals = emalloc(sizeof(bool) * 2);
        vals[0] = mapNode->keyMustBeFreed;
        vals[1] = mapNode->valueMustBeFreed;
        return vals;
    }
    for (StringHashMapNode *temp = mapNode->next; temp != NULL; temp = temp->next) {
        if (strcmp(key, temp->key) == 0) {
            bool *vals = emalloc(sizeof(bool) * 2);
            vals[0] = temp->keyMustBeFreed;
            vals[1] = temp->valueMustBeFreed;
            return vals;
        }
    }
    return NULL;
}

void StringHashMap_put(StringHashMap *map,
    char *key, bool keyMustBeFreed,
    char *value, bool valueMustBeFreed
) {
    unsigned long keyHash = hash(map, key);
    StringHashMapNode *mapNode = map->buckets[keyHash];
    if (mapNode == NULL) {
        StringHashMapNode *node = emalloc(sizeof(StringHashMapNode));
        node->key = key;
        node->value = value;
        node->next = NULL;
        node->keyMustBeFreed = keyMustBeFreed;
        node->valueMustBeFreed = valueMustBeFreed;
        map->buckets[keyHash] = node;
    } else if (strcmp(key, mapNode->key) != 0) {
        for (StringHashMapNode *temp = mapNode; ; temp = temp->next) {
            if (temp->next == NULL) {
                StringHashMapNode *node = emalloc(sizeof(StringHashMapNode));
                node->key = key;
                node->value = value;
                node->next = NULL;
                node->keyMustBeFreed = keyMustBeFreed;
                node->valueMustBeFreed = valueMustBeFreed;
                temp->next = node;
                break;
            } else if (strcmp(key, temp->key) == 0) {
                if (temp->valueMustBeFreed) {
                    free(temp->value);
                }
                temp->value = value;
                temp->valueMustBeFreed = valueMustBeFreed;
                break;
            }
        }
    } else {
        if (mapNode->valueMustBeFreed) {
            free(mapNode->value);
        }
        mapNode->value = value;
        mapNode->valueMustBeFreed = valueMustBeFreed;
    }
}

void StringHashMap_remove(StringHashMap *map, char *key) {
    unsigned long keyHash = hash(map, key);
    StringHashMapNode *mapNode = map->buckets[keyHash];
    if (mapNode == NULL) return;
    if (mapNode->next == NULL) {
        if (mapNode->keyMustBeFreed) {
            free(mapNode->key);
        }
        if (mapNode->valueMustBeFreed) {
            free(mapNode->value);
        }
        free(mapNode);
        map->buckets[keyHash] = NULL;
    } else {
        StringHashMapNode *prev = mapNode;
        for (StringHashMapNode *temp = mapNode->next; temp != NULL; temp = temp->next) {
            if (strcmp(key, temp->key) == 0) {
                prev->next = temp->next;
                if (temp->keyMustBeFreed) {
                    free(temp->key);
                }
                if (temp->valueMustBeFreed) {
                    free(temp->value);
                }
                free(temp);
                break;
            }
            prev = temp;
        }
    }
}

int StringHashMap_size(StringHashMap *map) {
    int size = 0;
    for (int i = 0; i < map->bucketSize; i++) {
        StringHashMapNode *temp = map->buckets[i];
        while (temp != NULL) {
            size++;
            temp = temp->next;
        }
    }
    return size;
}
