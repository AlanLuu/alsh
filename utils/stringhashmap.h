#ifndef ALSH_STRING_HASH_MAP_
#define ALSH_STRING_HASH_MAP_

#include <stdbool.h>

typedef struct StringHashMap StringHashMap;

StringHashMap* StringHashMap_createSize(int size);
StringHashMap* StringHashMap_create(void);
void StringHashMap_free(StringHashMap *map);

char*** StringHashMap_entries(StringHashMap *map);
char* StringHashMap_get(StringHashMap *map, char *key);
bool* StringHashMap_getMustBeFreed(StringHashMap *map, char *key);
void StringHashMap_put(StringHashMap* map,
    char *key, bool keyMustBeFreed, char *value, bool valueMustBeFreed);
void StringHashMap_remove(StringHashMap *map, char *key);
int StringHashMap_size(StringHashMap *map);

#endif
