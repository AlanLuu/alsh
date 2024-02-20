#ifndef ALSH_UTILS_
#define ALSH_UTILS_

#include <stdbool.h>
#include <sys/types.h>

#define SET_FUNCTION_STATUS(ptr, val) if (ptr != NULL) *ptr = val

//Returns the number of digits in a number
int numDigits(long num);

//Removes the newline character from the end of a string if it exists
void removeNewlineIfExists(char *str);

/**
 * Checks if a string array contains a particular string
 * Length of the array must be passed in to this function
*/
bool strArrContains(char **arr, char *str, size_t arrLen);

/**
 * Trims whitespace from the beginning and end of a string
 * If the string only contains whitespace, it will be trimmed to an empty string
 * Returns false if the string is empty, true otherwise
*/
bool trimWhitespaceFromEnds(char *str);

#endif
