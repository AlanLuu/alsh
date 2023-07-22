#include "ealloc.h"

#include <stdio.h>
#include <stdlib.h>

void exitIfNull(void *ptr) {
    if (ptr == NULL) {
        fprintf(stderr, "[FATAL] Failed to allocate memory, exiting program...\n");
        exit(1);
    }
}

void* ecalloc(size_t nmemb, size_t size) {
    void *ptr = calloc(nmemb, size);
    exitIfNull(ptr);
    return ptr;
}

void* emalloc(size_t size) {
    void *ptr = malloc(size);
    exitIfNull(ptr);
    return ptr;
}

void* erealloc(void *ptr, size_t size) {
    void *newPtr = realloc(ptr, size);
    exitIfNull(newPtr);
    return newPtr;
}
