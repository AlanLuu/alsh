#include "utils.h"

#include <string.h>

int numDigits(long num) {
    if (num < 0) num = -num;
    int count;
    for (count = 0; num > 0; count++, num /= 10);
    return count;
}

void removeNewlineIfExists(char *str) {
    size_t len = strlen(str);
    if (len > 0 && str[len - 1] == '\n') {
        str[len - 1] = '\0';
    }
}

bool strArrContains(char **arr, char *str, size_t arrLen) {
    for (size_t i = 0; i < arrLen; i++) {
        if (strcmp(str, arr[i]) == 0) {
            return true;
        }
    }
    return false;
}

bool trimWhitespaceFromEnds(char *str) {
    size_t len = strlen(str);
    if (len == 0) return false;

    bool hasWhitespace = false;
    size_t i = 0;
    if (str[i] == ' ') {
        hasWhitespace = true;
        while (str[i] == ' ') {
            i++;
        }
    }
    size_t j = len - 1;
    if (j > 0 && str[j] == ' ') {
        hasWhitespace = true;
        while (j > 0 && str[j] == ' ') {
            j--;
        }
    }

    if (hasWhitespace) {
        size_t k = 0;
        while (i <= j) {
            str[k] = str[i];
            i++;
            k++;
        }
        str[k] = '\0';
    }
    
    return true;
}
