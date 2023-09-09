#include <ctype.h>
#include <errno.h>
#include <pwd.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "utils/charlist.h"
#include "utils/ealloc.h"
#include "utils/stringlinkedlist.h"

#define COMMAND_BUFFER_SIZE 4096
#define COMMENT_CHAR '#'
#define CWD_BUFFER_SIZE 4096
#define EXIT_COMMAND "exit"
#define HISTORY_COMMAND "history"
#define HISTORY_FILE_NAME ".alsh_history"
#define SHELL_NAME "alsh"
#define STARTING_HISTORY_CAPACITY 25
#define USERNAME_MAX_LENGTH 32

#ifdef __linux__
#define IS_LINUX true
#else
#define IS_LINUX false
#endif

static char cwd[CWD_BUFFER_SIZE]; //Current working directory
static char *executablePath; //Path to where the current alsh shell executable is
static struct passwd *pwd; //User info

bool isInHomeDirectory(void) {
    char *cwdPtr = cwd;
    char *homeDirPtr = pwd->pw_dir;
    while (*homeDirPtr) {
        if (*cwdPtr++ != *homeDirPtr++) {
            return false;
        }
    }
    return true;
}
bool isRootUser(void) {
    return pwd->pw_uid == 0;
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

static bool sigintReceived = false;
void sigintHandler(int sig) {
    (void) sig;
    sigintReceived = true;
    (void) wait(NULL);
}

/**
 * Removes the newline character from the end of a string if it exists
*/
void removeNewlineIfExists(char *str) {
    size_t len = strlen(str);
    if (str[len - 1] == '\n') {
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
    bool inQuote = false;
    while (*tempStr) {
        if (*tempStr == '"') {
            inQuote = !inQuote;
        } else if (!inQuote && strstr(tempStr, delim) == tempStr) {
            *tempStr = '\0';
            tempStr = str;
            while (*tempStr) {
                if (*tempStr != '"') {
                    CharList_add(strList, *tempStr);
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

    if (inQuote) {
        fprintf(stderr, "%s: Missing closing quote\n", SHELL_NAME);
        StringLinkedList_free(tokens);
        CharList_free(strList);
        
        StringLinkedList *emptyTokens = StringLinkedList_create();
        return emptyTokens;
    }

    tempStr = str;
    while (*tempStr) {
        if (*tempStr != '"') {
            CharList_add(strList, *tempStr);
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
    char *stdoutRedirectChr = strchr(cmd, '>');
    int *status;
    if (stdoutRedirectChr != NULL) {
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
    char *stdinRedirectChr = strchr(cmd, '<');
    int *status;
    if (stdinRedirectChr != NULL) {
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

int executeCommand(char *cmd, bool waitForCommand) {
    if (cmd == NULL || !*cmd) {
        return 1;
    }

    int *stdinStatus = handleRedirectStdin(cmd);
    if (*stdinStatus == -1) {
        free(stdinStatus);
        return 1;
    }
    int *stdoutStatus = handleRedirectStdout(cmd);
    if (*stdoutStatus == -1) {
        free(stdinStatus);
        free(stdoutStatus);
        return 1;
    }
    
    int exitStatus = 0;
    CharList *tempCmd = CharList_create();
    char *cmdPtr = cmd;
    int cmdIndex = 0;
    while (*cmdPtr) {
        switch (*cmdPtr) {
            case '<':
            case '>': {
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
        return 1;
    }

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
        } else {
            temp = temp->next;
            tempNodeIndex++;
        }
    }

    StringNode *head = tokens->head;
    bool isBuiltInCommand = false;
    char *colorAutoCmds[] = {"ls", "grep"};
    if (head == NULL || strcmp(head->str, "false") == 0) {
        isBuiltInCommand = true;
        exitStatus = 1;
    } else if (strArrContains(colorAutoCmds, head->str, sizeof(colorAutoCmds) / sizeof(*colorAutoCmds))) {
        if (IS_LINUX) {
            StringLinkedList_append(tokens, "--color=auto", false);
        }
    } else if (strcmp(head->str, "true") == 0) {
        isBuiltInCommand = true;
        exitStatus = 0;
    } else if (strcmp(head->str, "cd") == 0) {
        isBuiltInCommand = true;
        StringNode *argNode = head->next;
        char *arg = argNode != NULL ? argNode->str : NULL;
        if (arg == NULL) { //No argument, change to home directory
            if (chdir(pwd->pw_dir) != 0) {
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
        char *err;
        switch (errno) {
            case ENOENT:
                err = "not found";
                break;
            case EACCES:
                err = "Permission denied";
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
                    char historyFile[USERNAME_MAX_LENGTH + 20];
                    strcpy(historyFile, pwd->pw_dir);
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
                    case ENOENT:
                        err = "command not found";
                        break;
                    case EACCES:
                        err = "Permission denied";
                        break;
                    default:
                        err = "Failed to execute command";
                        break;
                }
                fprintf(stderr, "%s: %s: %s\n", SHELL_NAME, head->str, err);
                exit(1);
            }
            if (waitForCommand) {
                int status;
                while (wait(&status) > 0);
                exitStatus = (WIFEXITED(status)) ? (WEXITSTATUS(status)) : 1;
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

        counter++;
        while (*counter == ' ') {
            counter++;
        }
        if (!isdigit(*counter)) {
            if (!*counter) {
                fprintf(stderr, "%s: syntax error: unexpected end of input, expected integer\n", SHELL_NAME);
            } else {
                fprintf(stderr, "%s: syntax error: unexpected token '%c'\n", SHELL_NAME, *counter);
            }
            return -1;
        }

        int loopAmount = 0;
        do {
            loopAmount = (loopAmount * 10) + (*counter - '0');
        } while (isdigit(*++counter));

        while (*counter == ' ') {
            counter++;
        }
        if (*counter != ')') {
            if (!*counter) {
                fprintf(stderr, "%s: syntax error: unexpected end of input, expected ')'\n", SHELL_NAME);
            } else {
                fprintf(stderr, "%s: syntax error: unexpected token '%c', expected ')'\n", SHELL_NAME, *counter);
            }
            return -1;
        }

        counter++;
        while (*counter == ' ') {
            counter++;
        }
        if (!*counter) {
            fprintf(stderr, "%s: syntax error: unexpected end of input\n", SHELL_NAME);
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
            : "%s:\033[1;34m~%s\033[0m$ ", SHELL_NAME, (cwd + strlen(pwd->pw_dir))
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
    if (argc > 1) {
        FILE *fp = fopen(argv[1], "r");
        if (fp == NULL) {
            fprintf(stderr, "%s: %s: No such file or directory\n", SHELL_NAME, argv[1]);
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
        history.capacity = STARTING_HISTORY_CAPACITY;
        history.elements = emalloc(sizeof(char*) * (size_t) history.capacity);

        //Total of 52 characters for /home/<username>/.alsh_history
        //Maximum of 32 characters for <username>
        //20 characters for the rest of the path
        char historyFile[USERNAME_MAX_LENGTH + 20];
        strcpy(historyFile, pwd->pw_dir);
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

        bool stdinFromTerminal = isatty(STDIN_FILENO);
        bool typedExitCommand = false;
        if (stdinFromTerminal) {
            struct sigaction sa = {
                .sa_handler = sigintHandler
            };
            sigemptyset(&sa.sa_mask);
            sigaction(SIGINT, &sa, NULL);

            printIntro();
            printPrompt();
        }

        //Ignore SIGINT so that the shell doesn't exit when user sends it
        //usually by pressing Ctrl+C
        do {
            sigintReceived = false;
            while (fgets(cmd, COMMAND_BUFFER_SIZE, stdin) != NULL) {
                removeNewlineIfExists(cmd);
                bool trimSuccess = trimWhitespaceFromEnds(cmd);
                if (*cmd && trimSuccess) {
                    int processHistoryStatus = processHistoryExclamations(cmd);
                    switch (processHistoryStatus) {
                        case 0:
                            printPrompt();
                            continue;
                        case 1:
                            printf("%s\n", cmd);
                            break;
                        default:
                            break;
                    }
                    addCommandToHistory(cmd);
                    if (*cmd != COMMENT_CHAR) {
                        size_t exitCmdLen = strlen(EXIT_COMMAND);
                        if (strncmp(cmd, EXIT_COMMAND, exitCmdLen) == 0
                            && (!cmd[exitCmdLen] || cmd[exitCmdLen] == ' ')
                        ) {
                            typedExitCommand = true;
                            break;
                        }
                        processCommand(cmd);
                    }
                }
                if (stdinFromTerminal) {
                    //sigintReceived will be true if the user sends SIGINT
                    //inside the shell prompt
                    if (sigintReceived) {
                        sigintReceived = false;
                        printf("\n");
                    }
                    printPrompt();
                }
            }

            //sigintReceived will be true if the user sends SIGINT
            //inside the shell prompt
            if (sigintReceived) {
                printf("\n");
                printPrompt();
            } else if (stdinFromTerminal) {
                if (!typedExitCommand) printf("\n");
                printf("%s\n", EXIT_COMMAND);
            }
        } while (sigintReceived);

        clearHistoryElements();
        free(history.elements);
    }
    free(cmd);
    return 0;
}
