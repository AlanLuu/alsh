#ifndef EALLOC_H_
#define EALLOC_H_

#include <sys/types.h>

void* ecalloc(size_t nmemb, size_t size);
void* emalloc(size_t size);
void* erealloc(void *ptr, size_t size);

#endif // EALLOC_H_
