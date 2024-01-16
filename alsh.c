#include <ctype.h>
#include <errno.h>
#include <pwd.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>

#include "utils/charlist.h"
#include "utils/doublelist.h"
#include "utils/ealloc.h"
#include "utils/mathparser.h"
#include "utils/stringhashmap.h"
#include "utils/stringlinkedlist.h"

#define BACKGROUND_CHAR '&'
#define COMMAND_BUFFER_SIZE 4096
#define COMMENT_CHAR '#'
#define CWD_BUFFER_SIZE 4096
#define EXIT_COMMAND "exit"
#define HISTORY_COMMAND "history"
#define HISTORY_FILE_NAME ".alsh_history"
#define SHELL_NAME "alsh"
#define STARTING_HISTORY_CAPACITY 25
#define TEST_COMMAND "chk"
#define USERNAME_MAX_LENGTH 32
#define VARIABLE_PREFIX '$'

#define MATH_PARSER_ERR_MSG(status) MathParser_printErrMsg(status, SHELL_NAME)

static StringHashMap *aliases; //Stores command aliases
static StringLinkedList *bgCmdDoneMessages; //Stores background command complete messages
static char cwd[CWD_BUFFER_SIZE]; //Current working directory
static char *executablePath; //Path to where the current alsh shell executable is
static bool isBackgroundCmd = false; //Did the user run a command in the background?
static int numBackgroundCmds = 0; //Number of background commands running
static struct passwd *pwd; //User info
static StringHashMap *variables; //Stores user-defined variables

extern char **environ;

char* getHomeDirectory(void) {
    return pwd != NULL ? pwd->pw_dir : getenv("HOME");
}
bool isInHomeDirectory(void) {
    char *cwdPtr = cwd;
    char *homeDirPtr = getHomeDirectory();
    while (*homeDirPtr) {
        if (*cwdPtr++ != *homeDirPtr++) {
            return false;
        }
    }
    return true;
}
bool isRootUser(void) {
    return getuid() == 0;
}

typedef struct History {
    char **elements;
    int count;
    int capacity;
} History;

History history;
void clearHistoryElements(void) {
    for (int i = 0; i < history.count; i++) {
        free(history.elements[i]);
    }
    history.count = 0;
}

/**
 * Returns the number of digits in a number
*/
int numDigits(long num) {
    if (num < 0) num = -num;
    int count;
    for (count = 0; num > 0; count++, num /= 10);
    return count;
}

static bool sigintReceived = false;
static bool sigchldReceived = false;
static int numSigchldBackground = 0;
void sigchldHandler(int sig) {
    (void) sig;
    if (sigintReceived) return;
    sigchldReceived = true;
    if (isBackgroundCmd) {
        if (numBackgroundCmds > 0) {
            numSigchldBackground++;
            pid_t cid = wait(NULL);
            size_t templateStrLen = strlen("[%d]+ Done with pid %d");
            size_t numSigchldBackgroundLen = (size_t) numDigits(numSigchldBackground);
            size_t cidLen = (size_t) numDigits(cid);
            char *buffer = emalloc(sizeof(char) * (templateStrLen + numSigchldBackgroundLen + cidLen));
            sprintf(buffer, "[%d]+ Done with pid %d", numSigchldBackground, cid);
            StringLinkedList_append(bgCmdDoneMessages, buffer, true);
            numBackgroundCmds--;
        }
        if (numBackgroundCmds == 0) {
            isBackgroundCmd = false;
            numSigchldBackground = 0;
        }
    }
}

void sigintHandler(int sig) {
    (void) sig;
    sigintReceived = true;
    if (numBackgroundCmds > 0) {
        while (wait(NULL) > 0) {
            numBackgroundCmds--;
        }
    } else {
        (void) wait(NULL);
    }
}

/**
 * If any background command complete message exists in bgCmdDoneMessages,
 * print all of them to stderr and pop the nodes containing the messages
 * This will also remove all nodes from bgCmdDoneMessages as a result
*/
void printBgCmdDoneMessageIfExists(void) {
    if (bgCmdDoneMessages != NULL) {
        while (bgCmdDoneMessages->head != NULL) {
            fprintf(stderr, "%s\n", bgCmdDoneMessages->head->str);
            StringLinkedList_removeIndexAndFreeNode(bgCmdDoneMessages, 0);
        }
    }
}

/**
 * Removes the newline character from the end of a string if it exists
*/
void removeNewlineIfExists(char *str) {
    size_t len = strlen(str);
    if (len > 0 && str[len - 1] == '\n') {
        str[len - 1] = '\0';
    }
}

/**
 * Splits a string from the first occurrence of delim and returns a StringLinkedList
 * pointer that refers to the first node of the StringLinkedList
 * Remember to free() the returned StringLinkedList
*/
StringLinkedList* split(char *str, char *delim) {
    StringLinkedList *tokens = StringLinkedList_create();
    CharList *strList = CharList_create();
    size_t delimLen = strlen(delim);
    char *tempStr = str;
    bool inSingleQuote = false;
    bool inDoubleQuote = false;
    int parenthesesNestLevel = 0;
    while (*tempStr) {
        if (!inDoubleQuote && parenthesesNestLevel == 0 && *tempStr == '\'') {
            inSingleQuote = !inSingleQuote;
        } else if (!inSingleQuote && parenthesesNestLevel == 0 && *tempStr == '"') {
            inDoubleQuote = !inDoubleQuote;
        } else if (!inDoubleQuote && !inSingleQuote && (*tempStr == '(' || *tempStr == ')')) {
            parenthesesNestLevel += *tempStr == '(' ? 1 : -1;
            if (parenthesesNestLevel < 0) {
                fprintf(stderr, "%s: Unexpected closing parentheses\n", SHELL_NAME);
                StringLinkedList_free(tokens);
                CharList_free(strList);
                
                StringLinkedList *emptyTokens = StringLinkedList_create();
                return emptyTokens;
            }
        } else if (
            !inSingleQuote &&
            !inDoubleQuote &&
            parenthesesNestLevel == 0 &&
            strstr(tempStr, delim) == tempStr
        ) {
            *tempStr = '\0';
            tempStr = str;
            while (*tempStr) {
                if (!inDoubleQuote && parenthesesNestLevel == 0 && *tempStr == '\'') {
                    inSingleQuote = !inSingleQuote;
                } else if (!inSingleQuote && parenthesesNestLevel == 0 && *tempStr == '"') {
                    inDoubleQuote = !inDoubleQuote;
                } else if (!inDoubleQuote && !inSingleQuote && (*tempStr == '(' || *tempStr == ')')) {
                    parenthesesNestLevel += *tempStr == '(' ? 1 : -1;
                    if (parenthesesNestLevel < 0) {
                        fprintf(stderr, "%s: Unexpected closing parentheses\n", SHELL_NAME);
                        StringLinkedList_free(tokens);
                        CharList_free(strList);
                        
                        StringLinkedList *emptyTokens = StringLinkedList_create();
                        return emptyTokens;
                    }
                }

                switch (*tempStr) {
                    case '"':
                        if (inSingleQuote || parenthesesNestLevel > 0) {
                            CharList_add(strList, *tempStr);
                        }
                        break;
                    case '\'':
                        if (inDoubleQuote || parenthesesNestLevel > 0) {
                            CharList_add(strList, *tempStr);
                        }
                        break;
                    default:
                        CharList_add(strList, *tempStr);
                        break;
                }

                tempStr++;
            }
            char *strListCopy = CharList_toStr(strList);
            StringLinkedList_append(tokens, strListCopy, true);
            CharList_clear(strList);
            str = tempStr;
            if (*delim == ' ' && !delim[1]) {
                do {
                    str++;
                } while (*str == ' ');
            } else {
                str += delimLen;
            }
            tempStr = str - 1;
        }
        tempStr++;
    }

    if (inSingleQuote || inDoubleQuote || parenthesesNestLevel != 0) {
        if (parenthesesNestLevel > 0) {
            fprintf(stderr, "%s: Missing closing parentheses\n", SHELL_NAME);
        } else if (parenthesesNestLevel < 0) {
            fprintf(stderr, "%s: Unexpected closing parentheses\n", SHELL_NAME);
        } else {
            fprintf(stderr, "%s: Missing closing quote\n", SHELL_NAME);
        }
        StringLinkedList_free(tokens);
        CharList_free(strList);
        
        StringLinkedList *emptyTokens = StringLinkedList_create();
        return emptyTokens;
    }

    tempStr = str;
    while (*tempStr) {
        if (!inDoubleQuote && parenthesesNestLevel == 0 && *tempStr == '\'') {
            inSingleQuote = !inSingleQuote;
        } else if (!inSingleQuote && parenthesesNestLevel == 0 && *tempStr == '"') {
            inDoubleQuote = !inDoubleQuote;
        } else if (!inDoubleQuote && !inSingleQuote && (*tempStr == '(' || *tempStr == ')')) {
            parenthesesNestLevel += *tempStr == '(' ? 1 : -1;
            if (parenthesesNestLevel < 0) {
                fprintf(stderr, "%s: Unexpected closing parentheses\n", SHELL_NAME);
                StringLinkedList_free(tokens);
                CharList_free(strList);
                
                StringLinkedList *emptyTokens = StringLinkedList_create();
                return emptyTokens;
            }
        }

        switch (*tempStr) {
            case '"':
                if (inSingleQuote || parenthesesNestLevel > 0) {
                    CharList_add(strList, *tempStr);
                }
                break;
            case '\'':
                if (inDoubleQuote || parenthesesNestLevel > 0) {
                    CharList_add(strList, *tempStr);
                }
                break;
            default:
                CharList_add(strList, *tempStr);
                break;
        }
        
        tempStr++;
    }
    
    char *strListCopy = CharList_toStr(strList);
    StringLinkedList_append(tokens, strListCopy, true);
    CharList_free(strList);
    return tokens;
}

/**
 * Checks if a string array contains a particular string
 * Length of the array must be passed in to this function
*/
bool strArrContains(char **arr, char *str, size_t arrLen) {
    for (size_t i = 0; i < arrLen; i++) {
        if (strcmp(str, arr[i]) == 0) {
            return true;
        }
    }
    return false;
}

/**
 * Trims whitespace from the beginning and end of a string
 * If the string only contains whitespace, it will be trimmed to an empty string
 * Returns false if the string is empty, true otherwise
*/
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

int* handleRedirectStdout(char *cmd) {
    char *stdoutRedirectChr = NULL;
    int *status;
    if (strchr(cmd, '>') != NULL) {
        bool inSingleQuote = false;
        bool inDoubleQuote = false;
        bool inParentheses = false;
        for (char *cmdPtr = cmd; *cmdPtr; cmdPtr++) {
            if (*cmdPtr == '\'') {
                if (!inDoubleQuote && !inParentheses) {
                    inSingleQuote = !inSingleQuote;
                }
            } else if (*cmdPtr == '"') {
                if (!inSingleQuote && !inParentheses) {
                    inDoubleQuote = !inDoubleQuote;
                }
            } else if (*cmdPtr == '(' || *cmdPtr == ')') {
                if (!inSingleQuote && !inDoubleQuote) {
                    inParentheses = *cmdPtr == '(';
                }
            }

            if (*cmdPtr == '>' && !inSingleQuote && !inDoubleQuote && !inParentheses) {
                stdoutRedirectChr = cmdPtr;
                break;
            }
        }
        if (stdoutRedirectChr == NULL) {
            status = ecalloc(1, sizeof(int));
            return status;
        }

        char *fileName = stdoutRedirectChr + 1;
        const char *fopenMode = "w";
        while (*fileName == ' ' || *fileName == '>') {
            if (*fileName == '>') fopenMode = "a";
            fileName++;
        }
        trimWhitespaceFromEnds(fileName);

        if (!*fileName) {
            fprintf(stderr, "%s: %s: Missing file name\n", SHELL_NAME, *fopenMode == 'a' ? ">>" : ">");
            status = emalloc(sizeof(int));
            *status = -1;
        } else {
            int oldStdout = dup(STDOUT_FILENO);
            status = emalloc(sizeof(int) * 3);
            *status = 1;
            status[1] = oldStdout;
            if (*cmd != '>' && isdigit(stdoutRedirectChr[-1])) {
                char *digit = stdoutRedirectChr - 1;
                do {
                    digit--;
                } while (isdigit(*digit));

                if (*digit == ' ') {
                    digit = stdoutRedirectChr - 1;
                    int fileDescriptor = 0;
                    int factor = 1;
                    do {
                        fileDescriptor += (*digit - '0') * factor;
                        factor *= 10;
                    } while (isdigit(*--digit));

                    status[2] = fileDescriptor;
                } else {
                    status[2] = STDOUT_FILENO;
                }
            } else {
                status[2] = STDOUT_FILENO;
            }
            
            FILE *fp = fopen(fileName, fopenMode);
            dup2(fileno(fp), status[2]);
            fclose(fp);
        }
    } else {
        status = ecalloc(1, sizeof(int));
    }
    return status;
}

int* handleRedirectStdin(char *cmd) {
    char *stdinRedirectChr = NULL;
    int *status;
    if (strchr(cmd, '<') != NULL) {
        bool inSingleQuote = false;
        bool inDoubleQuote = false;
        bool inParentheses = false;
        for (char *cmdPtr = cmd; *cmdPtr; cmdPtr++) {
            if (*cmdPtr == '\'') {
                if (!inDoubleQuote && !inParentheses) {
                    inSingleQuote = !inSingleQuote;
                }
            } else if (*cmdPtr == '"') {
                if (!inSingleQuote && !inParentheses) {
                    inDoubleQuote = !inDoubleQuote;
                }
            } else if (*cmdPtr == '(' || *cmdPtr == ')') {
                if (!inSingleQuote && !inDoubleQuote) {
                    inParentheses = *cmdPtr == '(';
                }
            }

            if (*cmdPtr == '<' && !inSingleQuote && !inDoubleQuote && !inParentheses) {
                stdinRedirectChr = cmdPtr;
                break;
            }
        }
        if (stdinRedirectChr == NULL) {
            status = ecalloc(1, sizeof(int));
            return status;
        }

        char *tempCmd = strdup(cmd);

        char *fileName = strchr(tempCmd, '<') + 1;
        while (*fileName == ' ') {
            fileName++;
        }
        trimWhitespaceFromEnds(fileName);

        char *stdoutRedirectChr = strchr(fileName, '>');
        if (stdoutRedirectChr != NULL) {
            do {
                stdoutRedirectChr--;
            } while (isdigit(*stdoutRedirectChr));

            //If n is the file descriptor to redirect to, parse "a < bn>c" as "a < bn > c"
            if (*stdoutRedirectChr == ' ') {
                do {
                    stdoutRedirectChr--;
                } while (*stdoutRedirectChr == ' ');

                stdoutRedirectChr++;
            } else {
                do {
                    stdoutRedirectChr++;
                } while (isdigit(*stdoutRedirectChr));
            }

            *stdoutRedirectChr = '\0';
        }

        FILE *fp = fopen(fileName, "r");
        if (fp == NULL) {
            fprintf(stderr, "%s: %s: No such file or directory\n", SHELL_NAME, fileName);
            status = emalloc(sizeof(int));
            *status = -1;
        } else {
            int oldStdin = dup(STDIN_FILENO);
            dup2(fileno(fp), STDIN_FILENO);
            fclose(fp);
            status = emalloc(sizeof(int) * 2);
            *status = 1;
            status[1] = oldStdin;
        }

        free(tempCmd);
    } else {
        status = ecalloc(1, sizeof(int));
    }
    return status;
}

char* processVariables(char *cmd);
int executeCommand(char *cmd, bool waitForCommand) {
    char *processVarCmd = NULL;
    if (cmd == NULL || !*cmd || (processVarCmd = processVariables(cmd)) == NULL) {
        return 1;
    }

    char *originalCmd = cmd;
    bool processedVars = processVarCmd != originalCmd;
    if (processedVars) {
        trimWhitespaceFromEnds(processVarCmd);
        cmd = processVarCmd;
    }

    int *stdinStatus = handleRedirectStdin(cmd);
    if (*stdinStatus == -1) {
        free(stdinStatus);
        if (processedVars) free(processVarCmd);
        return 1;
    }
    int *stdoutStatus = handleRedirectStdout(cmd);
    if (*stdoutStatus == -1) {
        free(stdinStatus);
        free(stdoutStatus);
        if (processedVars) free(processVarCmd);
        return 1;
    }
    
    int exitStatus = 0;
    CharList *tempCmd = CharList_create();
    bool inSingleQuote = false;
    bool inDoubleQuote = false;
    bool inParentheses = false;
    char *cmdPtr = cmd;
    int cmdIndex = 0;
    while (*cmdPtr) {
        switch (*cmdPtr) {
            case '\'': {
                if (!inDoubleQuote && !inParentheses) {
                    inSingleQuote = !inSingleQuote;
                }
                break;
            }
            case '"': {
                if (!inSingleQuote && !inParentheses) {
                    inDoubleQuote = !inDoubleQuote;
                }
                break;
            }
            case '(':
            case ')': {
                if (!inSingleQuote && !inDoubleQuote) {
                    inParentheses = !inParentheses;
                }
                break;
            }
        }
        switch (*cmdPtr) {
            case '<':
            case '>': {
                if (inSingleQuote || inDoubleQuote || inParentheses) {
                    CharList_add(tempCmd, *cmdPtr++);
                    break;
                }
                char *cmdPtrLeft = cmdPtr - 1;
                char *cmdPtrRight = cmdPtr + 1;
                bool noSpaceOnLeft = *cmdPtrLeft != ' ' && *cmdPtrLeft != '>';
                bool noSpaceOnRight = *cmdPtrRight != ' ' && *cmdPtrRight != '>';

                //Avoid ub if cmdPtr is at the beginning of the string
                if (cmdIndex > 0 && (noSpaceOnLeft || noSpaceOnRight)) {
                    if (noSpaceOnLeft
                        && (
                            !isdigit(*cmdPtrLeft)
                            || (
                                cmdIndex > 1
                                && *(cmdPtrLeft - 1) != ' '
                            )
                        )
                    ) {
                        CharList_add(tempCmd, ' ');
                    }

                    CharList_add(tempCmd, *cmdPtr++);
                    if (*++cmdPtrLeft == '>' && *cmdPtr == '>') {
                        CharList_add(tempCmd, *cmdPtr++);
                    }

                    //Do not put this line in any if statement so that a space is
                    //always added to the right of the rightmost redirection character
                    CharList_add(tempCmd, ' ');
                } else {
                    CharList_add(tempCmd, *cmdPtr++);
                }
                break;
            }
            default:
                CharList_add(tempCmd, *cmdPtr++);
                break;
        }
        cmdIndex++;
    }

    StringLinkedList *tokens = split(tempCmd->data, " ");
    if (tokens->size == 0) {
        if (*stdinStatus) {
            dup2(stdinStatus[1], STDIN_FILENO);
            close(stdinStatus[1]);
        }
        if (*stdoutStatus) {
            dup2(stdoutStatus[1], stdoutStatus[2]);
            close(stdoutStatus[1]);
        }
        free(stdinStatus);
        free(stdoutStatus);
        CharList_free(tempCmd);
        StringLinkedList_free(tokens);
        if (processedVars) free(processVarCmd);
        return 1;
    }

    bool isBuiltInCommand = false;
    int tempNodeIndex = 0;
    for (StringNode *temp = tokens->head; temp != NULL;) {
        char *strToRemove = temp->str;
        size_t strToRemoveLen = strlen(strToRemove);
        char lastChr = strToRemove[strToRemoveLen - 1];
        if (lastChr == '<' || lastChr == '>') {
            temp = temp->next;
            if (temp != NULL) {
                temp = temp->next;
                StringLinkedList_removeIndexAndFreeNode(tokens, tempNodeIndex);
            }
            StringLinkedList_removeIndexAndFreeNode(tokens, tempNodeIndex);
        } else if (strchr(strToRemove, '(') != NULL && temp->strMustBeFreed) {
            CharList *finalStrList = CharList_create();
            CharList *exprList = CharList_create();
            bool seenOtherChr = false;
            char *strToRemovePtr = strToRemove;
            while (*strToRemovePtr) {
                if (*strToRemovePtr == '(') {
                    int nestLevel = 1;
                    while (*++strToRemovePtr) {
                        if (*strToRemovePtr == '(') {
                            nestLevel++;
                        } else if (*strToRemovePtr == ')') {
                            nestLevel--;
                        }
                        if (nestLevel <= 0) break;
                        CharList_add(exprList, *strToRemovePtr);
                    }
                    char *expr = CharList_toStr(exprList);
                    trimWhitespaceFromEnds(expr);
                    int parseStatus;
                    double result = MathParser_parse(expr, &parseStatus);
                    free(expr);
                    if (MATH_PARSER_ERR_MSG(parseStatus)) {
                        if (*stdinStatus) {
                            dup2(stdinStatus[1], STDIN_FILENO);
                            close(stdinStatus[1]);
                        }
                        if (*stdoutStatus) {
                            dup2(stdoutStatus[1], stdoutStatus[2]);
                            close(stdoutStatus[1]);
                        }
                        free(stdinStatus);
                        free(stdoutStatus);
                        CharList_free(tempCmd);
                        StringLinkedList_free(tokens);
                        if (processedVars) free(processVarCmd);
                        CharList_free(finalStrList);
                        CharList_free(exprList);
                        return 1;
                    }
                    char *newStr = emalloc(sizeof(char) * 100);
                    sprintf(newStr, "%g", result);
                    CharList_addStr(finalStrList, newStr);
                    free(newStr);
                    CharList_clear(exprList);
                } else {
                    if (!seenOtherChr && !isdigit(*strToRemovePtr)) {
                        seenOtherChr = true;
                    }
                    CharList_add(finalStrList, *strToRemovePtr);
                }
                strToRemovePtr++;
            }
            char *finalStr = CharList_toStr(finalStrList);
            CharList_free(finalStrList);
            CharList_free(exprList);
            free(temp->str);
            temp->str = finalStr;
            temp = temp->next;
            if (temp == NULL && tempNodeIndex == 0 && !seenOtherChr) {
                printf("%s\n", finalStr);
                isBuiltInCommand = true;
            }
            tempNodeIndex++;
        } else {
            temp = temp->next;
            tempNodeIndex++;
        }
    }

    StringNode *head = tokens->head;
    if (aliases != NULL && head != NULL) {
        char *alias = StringHashMap_get(aliases, head->str);
        if (alias != NULL && strcmp(alias, head->str) != 0) {
            if (!*alias) {
                if (*stdinStatus) {
                    dup2(stdinStatus[1], STDIN_FILENO);
                    close(stdinStatus[1]);
                }
                if (*stdoutStatus) {
                    dup2(stdoutStatus[1], stdoutStatus[2]);
                    close(stdoutStatus[1]);
                }
                free(stdinStatus);
                free(stdoutStatus);
                CharList_free(tempCmd);
                StringLinkedList_free(tokens);
                if (processedVars) free(processVarCmd);
                return 1;
            }

            if (strchr(alias, ' ') != NULL) { //Alias value has space
                char *aliasDup = strdup(alias);
                StringLinkedList *aliasTokens = split(aliasDup, " ");
                char **aliasTokensArr = StringLinkedList_toArray(aliasTokens);
                StringLinkedList_removeIndexAndFreeNode(tokens, 0);
                for (int i = StringLinkedList_size(aliasTokens) - 1; i >= 0; i--) {
                    StringLinkedList_prepend(tokens, strdup(aliasTokensArr[i]), true);
                }
                free(aliasTokensArr);
                free(aliasDup);
                StringLinkedList_free(aliasTokens);
                head = tokens->head;
            } else {
                if (head->strMustBeFreed) {
                    free(head->str);
                }
                head->str = alias;
                bool *mustBeFreed = StringHashMap_getMustBeFreed(aliases, head->str);
                if (mustBeFreed != NULL) {
                    head->strMustBeFreed = mustBeFreed[1];
                    free(mustBeFreed);
                } else {
                    //Shouldn't happen
                    head->str = strdup(alias);
                }
            }
        }
    }

    bool isExport = false;
    if (head == NULL || strcmp(head->str, "false") == 0) {
        isBuiltInCommand = true;
        exitStatus = 1;
    } else if (strcmp(head->str, "true") == 0) {
        isBuiltInCommand = true;
        exitStatus = 0;
    } else if (strcmp(head->str, "cd") == 0) {
        isBuiltInCommand = true;
        StringNode *argNode = head->next;
        char *arg = argNode != NULL ? argNode->str : NULL;
        if (arg == NULL) { //No argument, change to home directory
            if (chdir(getHomeDirectory()) != 0) {
                //Should not happen
                fprintf(stderr, "%s: cd: Failed to change to home directory\n", SHELL_NAME);
                exitStatus = 1;
            }
        } else if (strcmp(arg, "..") == 0) { //Go up one directory
            char *lastSlashPos = strrchr(cwd, '/');
            *(lastSlashPos + 1) = '\0';
            if (chdir(cwd) != 0) {
                //Should not happen
                switch (errno) {
                    case EACCES:
                        fprintf(stderr, "%s: cd: %s: Permission denied\n", SHELL_NAME, arg);
                        break;
                    default:
                        fprintf(stderr, "%s: cd: Failed to change to parent directory\n", SHELL_NAME);
                        break;
                }
                exitStatus = 1;
            }
        } else if (chdir(arg) != 0) { //Change to specified directory
            char *err;
            switch (errno) {
                case EACCES:
                    err = "Permission denied";
                    break;
                case ENOENT:
                    err = "No such file or directory";
                    break;
                default:
                    err = "Failed to change to directory";
                    break;
            }
            fprintf(stderr, "%s: cd: %s: %s\n", SHELL_NAME, arg, err);
            exitStatus = 1;
        }
    } else if ((isExport = strcmp(head->str, "export") == 0) || strcmp(head->str, "let") == 0) {
        isBuiltInCommand = true;

        if (head->next == NULL) {
            if (isExport) {
                char **environPtr = environ;
                while (*environPtr) {
                    char *envStr = *environPtr++;
                    printf("export ");
                    while (*envStr && *envStr != '=') {
                        putchar(*envStr++);
                    }
                    putchar('=');
                    putchar('\'');
                    while (*++envStr) {
                        putchar(*envStr);
                    }
                    putchar('\'');
                    putchar('\n');
                }
            } else if (variables != NULL) {
                char ***keysVals = StringHashMap_entries(variables);
                int keysValsSize = StringHashMap_size(variables);
                for (int i = 0; i < keysValsSize; i++) {
                    printf("let %s=\"%s\"\n", keysVals[i][0], keysVals[i][1]);
                    free(keysVals[i]);
                }
                free(keysVals);
            }
        } else if (!isExport && variables == NULL) {
            variables = StringHashMap_create();
        }

        for (StringNode *argNode = head->next; argNode != NULL; argNode = argNode->next) {
            char *argStr = argNode->str;
            char *equalsSign = strchr(argStr, '=');
            if (equalsSign != NULL) {
                if (argStr[0] == '=') {
                    fprintf(stderr, "%s: %s: unexpected token '='", SHELL_NAME, isExport ? "export" : "let");
                    exitStatus = 1;
                    continue;
                }

                StringLinkedList *varList = split(argStr, "=");
                char *varKey = varList->head->str;
                char *varVal = varList->head->next->str;
                bool replacingLetVal = variables != NULL && StringHashMap_get(variables, varKey) != NULL;
                if (isExport) {
                    setenv(varKey, varVal, true);
                    if (replacingLetVal) {
                        StringHashMap_remove(variables, varKey);
                    }
                } else {
                    char *varKeyDup = strdup(varKey);
                    char *varValDup = strdup(varVal);
                    bool replacingExportVal = getenv(varKey) != NULL;
                    StringHashMap_put(variables, varKeyDup, true, varValDup, true);
                    if (replacingLetVal) {
                        free(varKeyDup);
                    }
                    if (replacingExportVal) {
                        unsetenv(varKey);
                    }
                }
                StringLinkedList_free(varList);
            } else if (isExport && variables != NULL) {
                char *letVal = StringHashMap_get(variables, argStr);
                if (letVal != NULL) {
                    setenv(argStr, letVal, true);
                    StringHashMap_remove(variables, argStr);
                }
            }
        }
    } else if (strcmp(head->str, TEST_COMMAND) == 0) {
        isBuiltInCommand = true;

        double first = 0;
        char *testCond = NULL;
        double second = 0;

        char *validTestOps[] = {"eq", "ne", "lt", "le", "gt", "ge"};
        size_t validTestOpsLen = sizeof(validTestOps) / sizeof(*validTestOps);

        StringNode *nextNode = head->next;
        bool testError = false;
        const int numArgs = 3;
        for (int i = 0; i < numArgs; i++) {
            char *errorStr = NULL;
            switch (i) {
                case 0: {
                    if (nextNode == NULL) {
                        fprintf(stderr, "%s: %s: Missing first value\n", SHELL_NAME, TEST_COMMAND);
                        testError = true;
                        break;
                    }
                    first = strtod(nextNode->str, &errorStr);
                    if (*errorStr) {
                        fprintf(stderr, "%s: %s: First value is not a number\n", SHELL_NAME, TEST_COMMAND);
                        testError = true;
                        break;
                    }
                    break;
                }
                case 1: {
                    if (nextNode == NULL) {
                        fprintf(stderr, "%s: %s: Missing test condition. ", SHELL_NAME, TEST_COMMAND);
                        fprintf(stderr, "Valid conditions include: ");
                        for (size_t j = 0; j < validTestOpsLen; j++) {
                            fprintf(stderr, "%s", validTestOps[j]);
                            if (j < validTestOpsLen - 1) {
                                fprintf(stderr, ", ");
                            }
                        }
                        fprintf(stderr, "\n");
                        testError = true;
                        break;
                    }
                    testCond = nextNode->str;
                    if (!strArrContains(validTestOps, testCond + (*testCond == '-'), validTestOpsLen)) {
                        fprintf(stderr, "%s: %s: Invalid test condition. ", SHELL_NAME, TEST_COMMAND);
                        fprintf(stderr, "Valid conditions include: ");
                        for (size_t j = 0; j < validTestOpsLen; j++) {
                            fprintf(stderr, "%s", validTestOps[j]);
                            if (j < validTestOpsLen - 1) {
                                fprintf(stderr, ", ");
                            }
                        }
                        fprintf(stderr, "\n");
                        testError = true;
                        break;
                    }
                    break;
                }
                case 2: {
                    if (nextNode == NULL) {
                        fprintf(stderr, "%s: %s: Missing second value\n", SHELL_NAME, TEST_COMMAND);
                        testError = true;
                        break;
                    }
                    second = strtod(nextNode->str, &errorStr);
                    if (*errorStr) {
                        fprintf(stderr, "%s: %s: Second value is not a number\n", SHELL_NAME, TEST_COMMAND);
                        testError = true;
                        break;
                    }
                    break;
                }
            }
            if (testError) break;
            nextNode = nextNode->next;
        }

        if (!testError) {
            bool containsDash = false;
            if (*testCond == '-') {
                containsDash = true;
                testCond++;
            }

            //0 denotes success, 1 denotes failure
            if (strcmp(testCond, validTestOps[0]) == 0) {
                exitStatus = !(fabs(first - second) < EPSILON);
            } else if (strcmp(testCond, validTestOps[1]) == 0) {
                exitStatus = !(fabs(first - second) >= EPSILON);
            } else if (strcmp(testCond, validTestOps[2]) == 0) {
                exitStatus = !(first < second);
            } else if (strcmp(testCond, validTestOps[3]) == 0) {
                exitStatus = !(first <= second);
            } else if (strcmp(testCond, validTestOps[4]) == 0) {
                exitStatus = !(first > second);
            } else {
                exitStatus = !(first >= second);
            }

            if (containsDash) testCond--;
        } else {
            exitStatus = 1;
        }
    } else if (strcmp(head->str, "alias") == 0) {
        isBuiltInCommand = true;
        if (head->next == NULL) {
            if (aliases != NULL) {
                char ***keysVals = StringHashMap_entries(aliases);
                int keysValsSize = StringHashMap_size(aliases);
                for (int i = 0; i < keysValsSize; i++) {
                    printf("alias %s=\"%s\"\n", keysVals[i][0], keysVals[i][1]);
                    free(keysVals[i]);
                }
                free(keysVals);
            }
        } else if (aliases == NULL) {
            aliases = StringHashMap_create();
        }
        for (StringNode *argNode = head->next; argNode != NULL; argNode = argNode->next) {
            char *argStr = argNode->str;
            char *equalsSign = strchr(argStr, '=');
            if (equalsSign != NULL) {
                if (argStr[0] == '=') {
                    fprintf(stderr, "%s: alias: %s: not found\n", SHELL_NAME, argStr);
                    exitStatus = 1;
                    continue;
                }
                StringLinkedList *aliasList = split(argStr, "=");
                char *aliasKey = aliasList->head->str;
                char *aliasVal = aliasList->head->next->str;
                char *aliasKeyDup = strdup(aliasKey);
                char *aliasValDup = strdup(aliasVal);
                bool replacingVal = StringHashMap_get(aliases, aliasKey) != NULL;
                StringHashMap_put(aliases, aliasKeyDup, true, aliasValDup, true);
                if (replacingVal) {
                    free(aliasKeyDup);
                }
                StringLinkedList_free(aliasList);
            } else {
                char *value = StringHashMap_get(aliases, argStr);
                if (value != NULL) {
                    printf("alias %s=\"%s\"\n", argStr, value);
                } else {
                    fprintf(stderr, "%s: alias: %s: not found\n", SHELL_NAME, argStr);
                    exitStatus = 1;
                }
            }
        }
    } else if (strcmp(head->str, "exec") == 0) {
        StringNode *argNode = head->next;
        StringLinkedList_removeIndexAndFreeNode(tokens, 0);
        char *command;
        if (argNode != NULL) {
            command = argNode->str;
        } else {
            StringLinkedList_append(tokens, executablePath, false);
            command = executablePath;
        }
        StringLinkedList_append(tokens, NULL, false);
        char **tokensArr = StringLinkedList_toArray(tokens);
        execvp(command, tokensArr);
        const char *isDirErr = "cannot execute: Is a directory";
        const char *err;
        struct stat statbuf;
        switch (errno) {
            case ENOENT: {
                char currentDirCmd[strlen(command) + 2 + 1];
                currentDirCmd[0] = '.';
                currentDirCmd[1] = '/';
                strcpy(currentDirCmd + 2, command);
                if (stat(currentDirCmd, &statbuf) == 0 && S_ISDIR(statbuf.st_mode)) {
                    err = isDirErr;
                } else {
                    err = "not found";
                }
                break;
            }
            case EACCES:
                if (stat(command, &statbuf) == 0 && S_ISDIR(statbuf.st_mode)) {
                    err = isDirErr;
                } else {
                    err = "Permission denied";
                }
                break;
            default:
                err = "Failed to execute command";
                break;
        }
        fprintf(stderr, "%s: exec: %s: %s\n", SHELL_NAME, command, err);
        free(tokensArr);
        isBuiltInCommand = true;
        exitStatus = 1;
    } else if (strcmp(head->str, HISTORY_COMMAND) == 0) {
        isBuiltInCommand = true;
        StringNode *argNode = head->next;
        char *flag = argNode != NULL ? argNode->str : NULL;
        if (flag != NULL) {
            char flagChr = flag[1];
            switch (flagChr) {
                case 'c':
                    clearHistoryElements();
                    break;
                case 'w': {
                    //Total of 52 characters for /home/<username>/.alsh_history
                    //Maximum of 32 characters for <username>
                    //20 characters for the rest of the path
                    //1 character for the null terminator
                    char historyFile[USERNAME_MAX_LENGTH + 20 + 1];
                    strcpy(historyFile, getHomeDirectory());
                    strcat(historyFile, "/" HISTORY_FILE_NAME);
                    FILE *historyfp = fopen(historyFile, "w");
                    if (historyfp == NULL) {
                        fprintf(stderr, "%s: %s: Failed to open history file\n", SHELL_NAME, HISTORY_COMMAND);
                        exitStatus = 1;
                    } else {
                        for (int i = 0; i < history.count; i++) {
                            fprintf(historyfp, "%s\n", history.elements[i]);
                        }
                        fclose(historyfp);
                    }
                    break;
                }
                default:
                    fprintf(stderr, "%s: %s: %s: invalid option\n", SHELL_NAME, HISTORY_COMMAND, flag);
                    exitStatus = 1;
                    break;
            }
        } else {
            for (int i = 0; i < history.count; i++) {
                printf("    %d. %s\n", i + 1, history.elements[i]);
            }
        }
    }

    if (!isBuiltInCommand) {
        pid_t cid = fork();
        if (cid >= 0) {
            if (cid == 0) {
                StringLinkedList_append(tokens, NULL, false);
                char **tokensArr = StringLinkedList_toArray(tokens);
                execvp(head->str, tokensArr);
                char *err;
                switch (errno) {
                    case ENOENT: {
                        char *headStr = head->str;
                        if (*headStr == '/' || (*headStr == '.' && headStr[1] == '/')) {
                            err = "No such file or directory";
                        } else {
                            err = "command not found";
                        }
                        break;
                    }
                    case EACCES: {
                        struct stat statbuf;
                        if (stat(head->str, &statbuf) == 0 && S_ISDIR(statbuf.st_mode)) {
                            err = "Is a directory";
                        } else {
                            err = "Permission denied";
                        }
                        break;
                    }
                    default:
                        err = "Failed to execute command";
                        break;
                }
                fprintf(stderr, "%s: %s: %s\n", SHELL_NAME, head->str, err);
                exit(1);
            }
            if (waitForCommand && !isBackgroundCmd) {
                int status;
                if (numBackgroundCmds > 0) {
                    while (waitpid(cid, &status, 0) > 0);
                } else {
                    while (wait(&status) > 0);
                }
                exitStatus = (WIFEXITED(status)) ? (WEXITSTATUS(status)) : 1;
            } else if (isBackgroundCmd) {
                numBackgroundCmds++;
                fprintf(stderr, "[%d] %d\n", numBackgroundCmds, cid);
            }
        } else {
            //Should not happen
            fprintf(stderr, "%s: Failed to spawn child process for command \"%s\"\n", SHELL_NAME, tempCmd->data);
            exitStatus = 1;
        }
    }
    
    if (*stdinStatus) {
        dup2(stdinStatus[1], STDIN_FILENO);
        close(stdinStatus[1]);
    }
    if (*stdoutStatus) {
        dup2(stdoutStatus[1], stdoutStatus[2]);
        close(stdoutStatus[1]);
    }
    free(stdinStatus);
    free(stdoutStatus);
    CharList_free(tempCmd);
    StringLinkedList_free(tokens);
    if (processedVars) free(processVarCmd);
    return exitStatus;
}

int processPipeCommands(char *cmd, char *orChr) {
    if (orChr != NULL) {
        char *tempCmd = strdup(cmd);
        StringLinkedList *tokens = split(tempCmd, "|");
        int terminal_stdin = dup(STDIN_FILENO);
        int terminal_stdout = dup(STDOUT_FILENO);
        int fd[2];
        StringNode *temp;
        bool isFirstIteration = true;
        bool pipeCommandFailed = false;
        for (temp = tokens->head; temp != tokens->tail; temp = temp->next) {
            if (pipe(fd) != 0) {
                //Should not happen
                fprintf(stderr, "%s: Failed to create pipe for command \"%s\" in \"%s\"\n", SHELL_NAME, temp->str, cmd);
                pipeCommandFailed = true;
                break;
            }
            pid_t cid = fork();
            if (cid < 0) {
                //Should not happen
                fprintf(stderr, "%s: Failed to spawn child process for command \"%s\" in \"%s\"\n", SHELL_NAME, temp->str, cmd);
                pipeCommandFailed = true;
                break;
            }
            if (cid == 0) {
                close(fd[0]);
                dup2(fd[1], STDOUT_FILENO);
                close(fd[1]);
                trimWhitespaceFromEnds(temp->str);
                (void) executeCommand(temp->str, false);
                exit(0);
            }
            close(fd[1]);
            dup2(fd[0], STDIN_FILENO);
            close(fd[0]);
            isFirstIteration = false;
            while (wait(NULL) > 0);
        }
        int exitStatus = 1;
        if (temp != NULL) {
            if (!pipeCommandFailed) {
                trimWhitespaceFromEnds(temp->str);
                exitStatus = executeCommand(temp->str, true);
            }
            if (!isFirstIteration) {
                dup2(terminal_stdout, STDOUT_FILENO);
                dup2(terminal_stdin, STDIN_FILENO);
            }
        }
        close(terminal_stdout);
        close(terminal_stdin);
        free(tempCmd);
        StringLinkedList_free(tokens);
        return exitStatus;
    }
    return executeCommand(cmd, true);
}

int processOrCommands(char *cmd) {
    char *orChr = strchr(cmd, '|');
    if (orChr != NULL && *(orChr + 1) == '|') {
        char *tempCmd = strdup(cmd);
        StringLinkedList *tokens = split(tempCmd, "||");
        for (StringNode *temp = tokens->head; temp != NULL; temp = temp->next) {
            trimWhitespaceFromEnds(temp->str);
            if (*temp->str) {
                int exitStatus = processPipeCommands(temp->str, strchr(temp->str, '|'));
                if (exitStatus == 0 || sigintReceived) {
                    free(tempCmd);
                    StringLinkedList_free(tokens);
                    return exitStatus;
                }
            }
        }
        free(tempCmd);
        StringLinkedList_free(tokens);
        return 1;
    }
    return processPipeCommands(cmd, orChr);
}

int processAndCommands(char *cmd) {
    char *andChr = strchr(cmd, '&');
    if (andChr != NULL && *(andChr + 1) == '&') {
        char *tempCmd = strdup(cmd);
        StringLinkedList *tokens = split(tempCmd, "&&");
        for (StringNode *temp = tokens->head; temp != NULL; temp = temp->next) {
            trimWhitespaceFromEnds(temp->str);
            if (*temp->str) {
                int exitStatus = processOrCommands(temp->str);
                if (exitStatus != 0) {
                    free(tempCmd);
                    StringLinkedList_free(tokens);
                    return exitStatus;
                }
            }
        }
        free(tempCmd);
        StringLinkedList_free(tokens);
        return 0;
    }
    return processOrCommands(cmd);
}

int processCommand(char *cmd) {
    //Check for comments
    char *commentChr = strchr(cmd, COMMENT_CHAR);
    if (commentChr != NULL && *(commentChr - 1) == ' ') {
        *commentChr = '\0';
    }

    //Syntax: if ([-]* <commandToTest>) <command1> [else <command2>]
    //Syntax: while ([-]* <commandToTest>) <command>
    const char *const ifStatement = "if";
    const char *const whileStatement = "while";
    size_t ifStatementLen = strlen(ifStatement);
    size_t whileStatementLen = strlen(whileStatement);
    bool isIfStatement = false;
    if ((isIfStatement = (strncmp(cmd, ifStatement, ifStatementLen) == 0
        && (
            !cmd[ifStatementLen]
            || cmd[ifStatementLen] == ' '
            || cmd[ifStatementLen] == '('
        ))) || (
            strncmp(cmd, whileStatement, whileStatementLen) == 0
            && (
                !cmd[whileStatementLen]
                || cmd[whileStatementLen] == ' '
                || cmd[whileStatementLen] == '('
            )
        )
    ) {
        char *counter = cmd + (isIfStatement ? ifStatementLen : whileStatementLen);
        while (*counter == ' ') {
            counter++;
        }
        if (*counter != '(') {
            if (!*counter) {
                fprintf(stderr, "%s: syntax error: unexpected end of input, expected '('\n", SHELL_NAME);
            } else {
                fprintf(stderr, "%s: syntax error: unexpected token '%c', expected '('\n", SHELL_NAME, *counter);
            }
            return -1;
        }

        do {
            counter++;
        } while (*counter == ' ');

        CharList *testCmdList = CharList_create();
        int nestLevel = 1;
        bool negate = false;
        do {
            if (*counter == '(') {
                nestLevel++;
            } else if (*counter == ')') {
                if (testCmdList->size == 0) {
                    fprintf(stderr, "%s: syntax error: unexpected token ')'\n", SHELL_NAME);
                    CharList_free(testCmdList);
                    return -1;
                } else {
                    nestLevel--;
                }
            } else if (!*counter) {
                if (testCmdList->size == 0) {
                    fprintf(stderr, "%s: syntax error: unexpected end of input, expected test condition\n", SHELL_NAME);
                } else {
                    fprintf(stderr, "%s: syntax error: unexpected end of input, expected ')'\n", SHELL_NAME);
                }
                CharList_free(testCmdList);
                return -1;
            }

            if (testCmdList->size == 0) {
                if (*counter == '-') {
                    negate = !negate;
                } else if (*counter != ' ') {
                    CharList_add(testCmdList, *counter);
                }
            } else if (nestLevel > 0) {
                CharList_add(testCmdList, *counter);
            }
            
            counter++;
        } while (nestLevel > 0);
        
        while (*counter == ' ') {
            counter++;
        }
        if (!*counter) {
            fprintf(stderr, "%s: syntax error: unexpected end of input, expected command after '%s'\n", SHELL_NAME, cmd);
            CharList_free(testCmdList);
            return -1;
        }

        char *testCmd = CharList_toStr(testCmdList);
        if (!*testCmd) {
            fprintf(stderr, "%s: syntax error: unexpected token ')'\n", SHELL_NAME);
            CharList_free(testCmdList);
            return -1;
        }

        trimWhitespaceFromEnds(testCmd);
        int status = processCommand(testCmd);
        bool ifCond = (!negate && status == 0) || (negate && status != 0);
        if (isIfStatement) {
            const char *const elseStatement = "else";
            size_t elseStatementLen = strlen(elseStatement);
            char *elseLocation = strstr(counter, elseStatement);
            if (elseLocation != NULL && *(elseLocation - 1) == ' ') {
                char *elseCounter = elseLocation + elseStatementLen;
                while (*elseCounter == ' ') {
                    elseCounter++;
                }
                if (!*elseCounter) {
                    fprintf(stderr, "%s: syntax error: unexpected end of input after '%s'\n", SHELL_NAME, elseStatement);
                    CharList_free(testCmdList);
                    return -1;
                }
                if (strncmp(elseCounter, ifStatement, ifStatementLen) != 0) {
                    char *nextElseLocation = strstr(elseCounter, elseStatement);
                    while (nextElseLocation != NULL && *(nextElseLocation - 1) == ' ') {
                        elseLocation = nextElseLocation;
                        elseCounter = elseLocation + elseStatementLen;
                        nextElseLocation = strstr(elseCounter, elseStatement);
                    }
                }

                if (ifCond) {
                    CharList *ifCounterList = CharList_create();
                    while (counter < elseLocation) {
                        CharList_add(ifCounterList, *counter++);
                    }

                    char *ifCounter = CharList_toStr(ifCounterList);
                    trimWhitespaceFromEnds(ifCounter);
                    (void) processCommand(ifCounter);

                    free(ifCounter);
                    CharList_free(ifCounterList);
                } else {
                    trimWhitespaceFromEnds(elseCounter);
                    (void) processCommand(elseCounter);
                }
            } else if (ifCond) {
                (void) processCommand(counter);
            }
        } else {
            while (ifCond) {
                if (sigintReceived || status < 0) {
                    break;
                }
                (void) processCommand(counter);
                status = processCommand(testCmd);
                ifCond = (!negate && status == 0) || (negate && status != 0);
            }
            if (!ifCond) status = 0;
        }

        free(testCmd);
        CharList_free(testCmdList);
        return status;
    }

    //Syntax: repeat (<integer>) <command>
    const char *const loopCommand = "repeat";
    size_t loopCommandLen = strlen(loopCommand);
    if (strncmp(cmd, loopCommand, loopCommandLen) == 0
        && (
            !cmd[loopCommandLen]
            || cmd[loopCommandLen] == ' '
            || cmd[loopCommandLen] == '('
        )
    ) {
        char *counter = cmd + loopCommandLen;
        while (*counter == ' ') {
            counter++;
        }
        if (*counter != '(') {
            if (!*counter) {
                fprintf(stderr, "%s: syntax error: unexpected end of input, expected '('\n", SHELL_NAME);
            } else {
                fprintf(stderr, "%s: syntax error: unexpected token '%c', expected '('\n", SHELL_NAME, *counter);
            }
            return -1;
        }

        do {
            counter++;
        } while (*counter == ' ');
        bool containsOperator = MathParser_containsOperator(counter);
        if (!containsOperator) {
            char *counterPtr = counter;
            while (*counterPtr) {
                if (*counterPtr++ == VARIABLE_PREFIX) {
                    while (*counterPtr == ' ') counterPtr++;
                    containsOperator = *counterPtr != ')';
                }
            }
        }
        if (!isdigit(*counter) && !containsOperator) {
            if (!*counter) {
                fprintf(stderr, "%s: syntax error: unexpected end of input, expected integer\n", SHELL_NAME);
            } else {
                fprintf(stderr, "%s: syntax error: unexpected token '%c'\n", SHELL_NAME, *counter);
            }
            return -1;
        }

        int loopAmount = 0;
        if (containsOperator) {
            CharList *exprList = CharList_create();
            int nestLevel = 1;
            do {
                switch (*counter) {
                    case '(':
                        nestLevel++;
                        break;
                    case ')':
                        nestLevel--;
                        break;
                }
                if (nestLevel <= 0) break;
                CharList_add(exprList, *counter++);
            } while (*counter);

            char *expr = CharList_toStr(exprList);
            if (CharList_contains(exprList, VARIABLE_PREFIX)) {
                char *oldExpr = expr;
                expr = processVariables(oldExpr);
                free(oldExpr);
            }
            CharList_free(exprList);
            int parseStatus;
            double result = MathParser_parse(expr, &parseStatus);
            free(expr);
            if (MATH_PARSER_ERR_MSG(parseStatus)) {
                return -1;
            }

            loopAmount = (int) result;
        } else {
            do {
                loopAmount = (loopAmount * 10) + (*counter - '0');
            } while (isdigit(*++counter));

            while (*counter == ' ') {
                counter++;
            }
        }

        if (*counter != ')') {
            if (!*counter) {
                fprintf(stderr, "%s: syntax error: unexpected end of input, expected ')'\n", SHELL_NAME);
            } else {
                fprintf(stderr, "%s: syntax error: unexpected token '%c', expected ')'\n", SHELL_NAME, *counter);
            }
            return -1;
        }

        do {
            counter++;
        } while (*counter == ' ');
        if (!*counter) {
            fprintf(stderr, "%s: syntax error: unexpected end of input, expected command after '%s'\n", SHELL_NAME, cmd);
            return -1;
        }

        for (int i = 0; i < loopAmount; i++) {
            int status = processCommand(counter);
            if (status < 0) { //status < 0 means a syntax error occurred
                return status;
            }
        }
        return 0;
    }

    //Check for semicolon operators
    char *semicolonChr = strchr(cmd, ';');
    if (semicolonChr != NULL) {
        char *tempCmd = strdup(cmd);
        StringLinkedList *tokens = split(tempCmd, ";");
        for (StringNode *temp = tokens->head; temp != NULL; temp = temp->next) {
            trimWhitespaceFromEnds(temp->str);
            (void) processAndCommands(temp->str);
        }
        free(tempCmd);
        StringLinkedList_free(tokens);
        return 0;
    }

    return processAndCommands(cmd);
}

void addCommandToHistory(char *cmd) {
    //Don't add the history command to the history array if it's the latest command
    //in the array and the user types it again
    if (history.count > 0) {
        char *lastElement = history.elements[history.count - 1];
        if (strcmp(cmd, HISTORY_COMMAND) == 0 && strcmp(lastElement, cmd) == 0) {
            return;
        }
    }

    if (history.count == history.capacity) {
        history.capacity *= 2;
        history.elements = erealloc(history.elements, sizeof(char*) * (size_t) history.capacity);
    }
    history.elements[history.count++] = strdup(cmd);
}

int processHistoryExclamations(char *cmd) {
    if (strchr(cmd, '!') != NULL) {
        CharList *tempCmd = CharList_create();
        char *cmdCounter = cmd;
        
        while (*cmdCounter) {
            if (*cmdCounter == '!') {
                if (!*(cmdCounter + 1)) {
                    if (*cmd == '!') { //Only ! in command
                        CharList_free(tempCmd);
                        return 0;
                    } else { //Single ! at the end of command
                        CharList_add(tempCmd, *cmdCounter++);
                        strcpy(cmd, tempCmd->data);
                        CharList_free(tempCmd);
                        return -1;
                    }
                }

                bool isNegative = false;
                cmdCounter++;
                if (*cmdCounter == '-') { //!-<number> command
                    isNegative = true;
                    cmdCounter++;
                }

                if (*cmdCounter == '!') { //!! command
                    if (history.count == 0) {
                        fprintf(stderr, "%s: !!: event not found\n", SHELL_NAME);
                        CharList_free(tempCmd);
                        return 0;
                    }

                    char *historyCmd = history.elements[history.count - 1];
                    while (*historyCmd) {
                        CharList_add(tempCmd, *historyCmd++);
                    }
                    cmdCounter++;
                } else if (isdigit(*cmdCounter)) { //!<number> command
                    int historyNumber;
                    for (historyNumber = 0; isdigit(*cmdCounter); cmdCounter++) {
                        historyNumber = historyNumber * 10 + *cmdCounter - '0';
                    }

                    int historyIndex;
                    if (isNegative) {
                        historyIndex = history.count - historyNumber;
                        if (historyIndex < 0) {
                            fprintf(stderr, "%s: !-%d: event not found\n", SHELL_NAME, historyNumber);
                            CharList_free(tempCmd);
                            return 0;
                        }
                    } else {
                        if (historyNumber <= 0 || historyNumber > history.count) {
                            fprintf(stderr, "%s: !%d: event not found\n", SHELL_NAME, historyNumber);
                            CharList_free(tempCmd);
                            return 0;
                        }
                        historyIndex = historyNumber - 1;
                    }

                    char *historyCmd = history.elements[historyIndex];
                    while (*historyCmd) {
                        CharList_add(tempCmd, *historyCmd++);
                    }
                } else {
                    cmdCounter -= isNegative ? 2 : 1;
                    fprintf(stderr, "%s: %s: event not found\n", SHELL_NAME, cmdCounter);
                    CharList_free(tempCmd);
                    return 0;
                }
            } else {
                CharList_add(tempCmd, *cmdCounter++);
            }
        }

        strcpy(cmd, tempCmd->data);
        CharList_free(tempCmd);
        return 1;
    }

    return -1;
}

char* processVariables(char *cmd) {
    if (strchr(cmd, VARIABLE_PREFIX) != NULL && cmd[1]) {
        CharList *tempCmd = CharList_create();
        bool inParentheses = false;
        char *cmdCounter = cmd;

        while (*cmdCounter) {
            switch (*cmdCounter) {
                case '(':
                    inParentheses = true;
                    break;
                case ')':
                    inParentheses = false;
                    break;
            }
            if (*cmdCounter == VARIABLE_PREFIX) {
                CharList *varNameList = NULL;
                bool inVarLoop = false;
                while (
                    *++cmdCounter
                    && *cmdCounter != ' '
                    && *cmdCounter != ')'
                    && *cmdCounter != '"'
                    && *cmdCounter != ';'
                    && *cmdCounter != '&'
                    && *cmdCounter != '|'
                    && !MathParser_isAnyOperator(*cmdCounter)
                    && *cmdCounter != VARIABLE_PREFIX
                ) {
                    inVarLoop = true;
                    if (varNameList == NULL) {
                        varNameList = CharList_create();
                    }
                    CharList_add(varNameList, *cmdCounter);
                }
                if (!inVarLoop) continue;

                char *varKey = varNameList->data;
                char *varValue = getenv(varKey);
                if (varValue == NULL && variables != NULL) {
                    varValue = StringHashMap_get(variables, varKey);
                }

                if (varValue != NULL) {
                    CharList_addStr(tempCmd, varValue);
                } else if (inParentheses) {
                    fprintf(stderr, "%s: name error: %s is not defined\n", SHELL_NAME, varKey);
                    CharList_free(varNameList);
                    CharList_free(tempCmd);
                    return NULL;
                }
                CharList_free(varNameList);
            } else {
                CharList_add(tempCmd, *cmdCounter++);
            }
        }
        
        char *newCmd = CharList_toStr(tempCmd);
        CharList_free(tempCmd);
        return newCmd;
    }

    return cmd;
}

void printIntro(void) {
    printf("Welcome to %s!\n", SHELL_NAME);
    printf("Type '%s' to exit.\n\n", EXIT_COMMAND);
}

void printPrompt(void) {
    if (getcwd(cwd, CWD_BUFFER_SIZE) == NULL) {
        fprintf(stderr, "%s: Error getting current working directory, exiting shell...\n", SHELL_NAME);
        exit(1);
    }

    //If in home directory, print ~ instead of /home/<username>
    //Print red prompt if user is root, otherwise print regular prompt
    if (isInHomeDirectory()) {
        printf(isRootUser()
            ? "\033[38;5;196;1m%s-root:\033[1;34m~%s\033[0m# "
            : "%s:\033[1;34m~%s\033[0m$ ", SHELL_NAME, (cwd + strlen(getHomeDirectory()))
        );
    } else {
        printf(isRootUser()
            ? "\033[38;5;196;1m%s-root:\033[1;34m%s\033[0m# "
            : "%s:\033[1;34m%s\033[0m$ ", SHELL_NAME, cwd
        );
    }
}

int main(int argc, char *argv[]) {
    char *cmd = emalloc(sizeof(char) * COMMAND_BUFFER_SIZE);
    executablePath = argv[0];
    pwd = getpwuid(getuid());
    if (pwd == NULL && getHomeDirectory() == NULL) {
        fprintf(stderr, "%s: Could not determine home directory\n", SHELL_NAME);
        fprintf(stderr, "Please make sure the HOME environment variable is defined\n");
        free(cmd);
        exit(1);
    }
    
    if (argc > 1) {
        FILE *fp = fopen(argv[1], "r");
        if (fp == NULL) {
            fprintf(stderr, "%s: %s: No such file or directory\n", SHELL_NAME, argv[1]);
            free(cmd);
            exit(1);
        }
        while (fgets(cmd, COMMAND_BUFFER_SIZE, fp) != NULL) {
            removeNewlineIfExists(cmd);
            bool trimSuccess = trimWhitespaceFromEnds(cmd);
            if (*cmd && *cmd != COMMENT_CHAR && trimSuccess) {
                (void) processCommand(cmd);
            }
        }
        fclose(fp);
    } else {
        bool stdinFromTerminal = isatty(STDIN_FILENO);
        if (stdinFromTerminal) {
            history.capacity = STARTING_HISTORY_CAPACITY;
            history.elements = emalloc(sizeof(char*) * (size_t) history.capacity);

            //Total of 53 characters for /home/<username>/.alsh_history
            //Maximum of 32 characters for <username>
            //7 characters for /home//
            //13 characters for the file name
            //1 character for the null terminator
            char historyFile[7 + USERNAME_MAX_LENGTH + 13 + 1];
            strcpy(historyFile, getHomeDirectory());
            strcat(historyFile, "/" HISTORY_FILE_NAME);
            FILE *historyfp = fopen(historyFile, "r");
            if (historyfp != NULL) {
                while (fgets(cmd, COMMAND_BUFFER_SIZE, historyfp) != NULL) {
                    removeNewlineIfExists(cmd);
                    bool trimSuccess = trimWhitespaceFromEnds(cmd);
                    if (*cmd && *cmd != COMMENT_CHAR && trimSuccess) {
                        addCommandToHistory(cmd);
                    }
                }
                fclose(historyfp);
            }

            //Total of 47 characters for /home/<username>/.alshrc
            //Maximum of 32 characters for <username>
            //7 characters for /home//
            //7 characters for the file name
            //1 character for the null terminator
            char alshrc[7 + USERNAME_MAX_LENGTH + 7 + 1];
            strcpy(alshrc, getHomeDirectory());
            strcat(alshrc, "/.alshrc");
            FILE *alshrcfp = fopen(alshrc, "r");
            if (alshrcfp != NULL) {
                while (fgets(cmd, COMMAND_BUFFER_SIZE, alshrcfp) != NULL) {
                    removeNewlineIfExists(cmd);
                    bool trimSuccess = trimWhitespaceFromEnds(cmd);
                    if (*cmd && *cmd != COMMENT_CHAR && trimSuccess) {
                        (void) processCommand(cmd);
                    }
                }
                fclose(alshrcfp);
            }

            struct sigaction sa1 = {
                .sa_handler = sigintHandler
            };
            struct sigaction sa2 = {
                .sa_handler = sigchldHandler
            };
            sigemptyset(&sa1.sa_mask);
            sigemptyset(&sa2.sa_mask);
            sigaction(SIGINT, &sa1, NULL);
            sigaction(SIGCHLD, &sa2, NULL);

            setvbuf(stdout, NULL, _IONBF, 0);
            printIntro();
            printPrompt();
        }

        //Ignore SIGINT so that the shell doesn't exit when user sends it
        //usually by pressing Ctrl+C
        bool typedExitCommand = false;
        do {
            sigintReceived = false;
            sigchldReceived = false;
            while (fgets(cmd, COMMAND_BUFFER_SIZE, stdin) != NULL) {
                printBgCmdDoneMessageIfExists();
                removeNewlineIfExists(cmd);
                bool trimSuccess = trimWhitespaceFromEnds(cmd);
                if (*cmd && trimSuccess) {
                    bool fgAfterBgCmd = true;
                    if (stdinFromTerminal) {
                        int processHistoryStatus = processHistoryExclamations(cmd);
                        switch (processHistoryStatus) {
                            case 0: //History event not found using !n or !-n
                                printPrompt();
                                continue;
                            case 1: //History event found using !n or !-n
                                printf("%s\n", cmd);
                                break;
                            default: //No history event specified using !n or !-n
                                break;
                        }
                        addCommandToHistory(cmd);
                        size_t cmdLen = strlen(cmd);
                        bool runCmdInBackground = cmdLen > 1
                            && cmd[cmdLen - 1] == BACKGROUND_CHAR
                            && cmd[cmdLen - 2] != BACKGROUND_CHAR;
                        if (runCmdInBackground) {
                            if (!isBackgroundCmd) {
                                isBackgroundCmd = true;
                            } else {
                                fgAfterBgCmd = false;
                            }

                            //Only initialize bgCmdDoneMessages when the user
                            //executes a background command in a shell session
                            if (bgCmdDoneMessages == NULL) {
                                bgCmdDoneMessages = StringLinkedList_create();
                            }
                            
                            cmd[--cmdLen] = '\0';
                            while (cmd[--cmdLen] == ' ') {
                                cmd[cmdLen] = '\0';
                            }
                        }
                    }
                    if (*cmd != COMMENT_CHAR) {
                        size_t exitCmdLen = strlen(EXIT_COMMAND);
                        if (strncmp(cmd, EXIT_COMMAND, exitCmdLen) == 0
                            && (!cmd[exitCmdLen] || cmd[exitCmdLen] == ' ')
                        ) {
                            typedExitCommand = true;
                            break;
                        }

                        if (fgAfterBgCmd && numBackgroundCmds > 0) {
                            //User runs foreground commands after background commands
                            isBackgroundCmd = false;
                            processCommand(cmd);
                            isBackgroundCmd = true;
                        } else {
                            int cmdStatus = processCommand(cmd);
                            if (cmdStatus == -1 && numBackgroundCmds == 0 && isBackgroundCmd) {
                                //Syntax error with BACKGROUND_CHAR at end of command
                                isBackgroundCmd = false;
                            }
                        }
                    }
                }
                if (stdinFromTerminal) {
                    //sigintReceived will be true if the user sends SIGINT
                    //inside the shell prompt
                    if (sigintReceived) {
                        sigintReceived = false;
                        printf("\n");
                    }
                    if (sigchldReceived) {
                        sigchldReceived = false;
                    }
                    printPrompt();
                }
            }

            //sigintReceived will be true if the user sends SIGINT
            //inside the shell prompt
            if (sigintReceived || sigchldReceived) {
                if (sigintReceived) {
                    printf("\n");
                    printBgCmdDoneMessageIfExists();
                    printPrompt();
                }
            } else if (stdinFromTerminal) {
                if (!typedExitCommand) printf("\n");
                printf("%s\n", EXIT_COMMAND);
            }
        } while (sigintReceived || sigchldReceived);

        //Kill any remaining background processes on shell exit
        if (numBackgroundCmds > 0) {
            signal(SIGTERM, SIG_IGN);
            kill(0, SIGTERM);
        }

        if (bgCmdDoneMessages != NULL) {
            StringLinkedList_free(bgCmdDoneMessages);
        }

        clearHistoryElements();
        free(history.elements);
    }

    StringHashMap *hashMapsToFree[] = {aliases, variables};
    for (size_t i = 0; i < sizeof(hashMapsToFree) / sizeof(*hashMapsToFree); i++) {
        if (hashMapsToFree[i] != NULL) {
            StringHashMap_free(hashMapsToFree[i]);
        }
    }

    free(cmd);
    return 0;
}
