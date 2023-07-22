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

#include "utils/ealloc.h"
#include "utils/stringlinkedlist.h"

#define COMMAND_BUFFER_SIZE 4096
#define COMMENT_CHAR '#'
#define CWD_BUFFER_SIZE 4096
#define EXIT_COMMAND "exit"
#define HISTORY_COMMAND "history"
#define HISTORY_FILE_NAME ".alsh_history"
#define HISTORY_MAX_ELEMENTS 100
#define SHELL_NAME "alsh"
#define USERNAME_MAX_LENGTH 32

static char cwd[CWD_BUFFER_SIZE]; //Current working directory
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

struct {
    char *elements[HISTORY_MAX_ELEMENTS];
    int count;
} history;
void freeHistoryElements(void) {
    for (int i = 0; i < history.count; i++) {
        free(history.elements[i]);
    }
}

static bool sigintReceived = false;
void sigintHandler(int sig) {
    (void) sig;
    sigintReceived = true;
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
 * Removes the first occurrence of str from tokens if it exists
*/
void removeStrFromArrIfExists(char **tokens, char *str) {
    for (int i = 0; tokens[i] != NULL; i++) {
        if (strcmp(tokens[i], str) == 0) {
            tokens[i] = NULL;
            break;
        }
    }
}

/**
 * Splits a string from the first occurrence of delim and returns a StringLinkedList
 * pointer that refers to the first node of the StringLinkedList
 * Remember to free() the returned StringLinkedList
*/
StringLinkedList* split(char *str, char *delim) {
    StringLinkedList *tokens = StringLinkedList_create();
    char *token = strtok(str, delim);
    while (token != NULL) {
        StringLinkedList_append(tokens, token, false);
        token = strtok(NULL, delim);
    }
    return tokens;
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
        char *fopenMode = "w";
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
            status = emalloc(sizeof(int) * 2);
            *status = 1;
            status[1] = oldStdout;
            
            FILE *fp = fopen(fileName, fopenMode);
            dup2(fileno(fp), STDOUT_FILENO);
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
            } while (*stdoutRedirectChr == ' ');
            
            stdoutRedirectChr++;
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

int executeCommand(char *cmd) {
    if (cmd == NULL || !*cmd) {
        return 1;
    }
    int exitStatus = 0;
    char *tempCmd = emalloc(sizeof(char) * COMMAND_BUFFER_SIZE);
    char *cmdPtr = cmd;
    char *tempCmdPtr = tempCmd;
    int cmdIndex = 0;
    while (*cmdPtr) {
        switch (*cmdPtr) {
            case '<':
            case '>': {
                bool noSpaceOnLeft = *(cmdPtr - 1) != ' ' && *(cmdPtr - 1) != '>';
                bool noSpaceOnRight = *(cmdPtr + 1) != ' ' && *(cmdPtr + 1) != '>';

                //Avoid ub if cmdPtr is at the beginning of the string
                if (cmdIndex > 0 && (noSpaceOnLeft || noSpaceOnRight)) {
                    if (noSpaceOnLeft) {
                        *tempCmdPtr++ = ' ';
                    }
                    *tempCmdPtr++ = *cmdPtr++;
                    if (*(cmdPtr - 1) == '>' && *cmdPtr == '>') {
                        *tempCmdPtr++ = *cmdPtr++;
                    }
                    *tempCmdPtr++ = ' ';
                } else {
                    *tempCmdPtr++ = *cmdPtr++;
                }
                break;
            }
            default:
                *tempCmdPtr++ = *cmdPtr++;
                break;
        }
        cmdIndex++;
    }
    *tempCmdPtr = '\0';
    
    StringLinkedList *tokens = split(tempCmd, " ");

    //Add --color=auto to ls command
    if (strcmp(tokens->head->str, "ls") == 0) {
        StringLinkedList_append(tokens, "--color=auto", false);
    }

    //cd command
    if (strcmp(tokens->head->str, "cd") == 0) {
        StringNode *argNode = tokens->head->next;
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
        free(tempCmd);
        StringLinkedList_free(tokens);
        return exitStatus;
    }

    //history command
    if (strcmp(tokens->head->str, HISTORY_COMMAND) == 0) {
        StringNode *argNode = tokens->head->next;
        char *flag = argNode != NULL ? argNode->str : NULL;
        if (flag != NULL) {
            char flagChr = flag[1];
            switch (flagChr) {
                case 'c':
                    freeHistoryElements();
                    history.count = 0;
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
        free(tempCmd);
        StringLinkedList_free(tokens);
        return exitStatus;
    }

    int *stdinStatus = handleRedirectStdin(cmd);
    if (*stdinStatus == -1) {
        free(stdinStatus);
        free(tempCmd);
        StringLinkedList_free(tokens);
        return 1;
    }
    int *stdoutStatus = handleRedirectStdout(cmd);
    if (*stdoutStatus == -1) {
        free(stdinStatus);
        free(stdoutStatus);
        free(tempCmd);
        StringLinkedList_free(tokens);
        return 1;
    }

    pid_t cid = fork();
    if (cid == 0) {
        char *strsToRemove[] = {"<", ">", ">>"};
        for (size_t i = 0; i < sizeof(strsToRemove) / sizeof(*strsToRemove); i++) {
            char *strToRemove = strsToRemove[i];
            int strToRemoveIndex = StringLinkedList_indexOf(tokens, strToRemove);
            if (strToRemoveIndex != -1) {
                (void) StringLinkedList_removeIndex(tokens, strToRemoveIndex);
                (void) StringLinkedList_removeIndex(tokens, strToRemoveIndex);
            }
        }
        StringLinkedList_append(tokens, NULL, false);
        char **tokensArr = StringLinkedList_toArray(tokens);
        execvp(tokens->head->str, tokensArr);
        fprintf(stderr, "%s: command not found\n", tokens->head->str);
        exit(1);
    }

    int status;
    while (wait(&status) > 0);

    exitStatus = (WIFEXITED(status)) ? (WEXITSTATUS(status)) : 1;
    if (*stdinStatus) {
        dup2(stdinStatus[1], STDIN_FILENO);
        close(stdinStatus[1]);
    }
    if (*stdoutStatus) {
        dup2(stdoutStatus[1], STDOUT_FILENO);
        close(stdoutStatus[1]);
    }
    free(stdinStatus);
    free(stdoutStatus);
    free(tempCmd);
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
        for (temp = tokens->head; temp != tokens->tail; temp = temp->next) {
            if (pipe(fd) != 0) {
                //Should not happen
                fprintf(stderr, "%s: Failed to create pipe\n", SHELL_NAME);
                free(tempCmd);
                StringLinkedList_free(tokens);
                return 1;
            }
            pid_t cid = fork();
            if (cid == 0) {
                dup2(fd[1], STDOUT_FILENO);
                close(fd[0]);
                (void) executeCommand(temp->str);
                exit(0);
            }
            while (wait(NULL) > 0);
            dup2(fd[0], STDIN_FILENO);
            close(fd[1]);
        }
        int exitStatus = executeCommand(temp->str);
        dup2(terminal_stdout, STDOUT_FILENO);
        dup2(terminal_stdin, STDIN_FILENO);
        close(terminal_stdout);
        close(terminal_stdin);
        free(tempCmd);
        StringLinkedList_free(tokens);
        return exitStatus;
    }
    return executeCommand(cmd);
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

void processCommand(char *cmd) {
    //Check for comments
    char *commentChr = strchr(cmd, COMMENT_CHAR);
    if (commentChr != NULL && *(commentChr - 1) == ' ') {
        *commentChr = '\0';
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
        return;
    }

    (void) processAndCommands(cmd);
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

    if (history.count == HISTORY_MAX_ELEMENTS) {
        free(history.elements[0]);

        //Shift all elements to the left
        for (int i = 0; i < HISTORY_MAX_ELEMENTS - 1; i++) {
            history.elements[i] = history.elements[i + 1];
        }
        history.elements[HISTORY_MAX_ELEMENTS - 1] = strdup(cmd);
    } else {
        history.elements[history.count] = strdup(cmd);
        history.count++;
    }
}

int processHistoryExclamations(char *cmd) {
    if (strchr(cmd, '!') != NULL) {
        char *tempCmd = emalloc(sizeof(char) * COMMAND_BUFFER_SIZE);
        char *cmdCounter = cmd;
        char *tempCmdCounter = tempCmd;

        while (*cmdCounter) {
            if (*cmdCounter == '!') {
                if (!*(cmdCounter + 1)) {
                    if (*cmd == '!') { //Only ! in command
                        free(tempCmd);
                        return 0;
                    } else { //Single ! at the end of command
                        *tempCmdCounter++ = *cmdCounter++;
                        *tempCmdCounter = '\0';
                        strcpy(cmd, tempCmd);
                        free(tempCmd);
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
                        free(tempCmd);
                        return 0;
                    }

                    char *historyCmd = history.elements[history.count - 1];
                    while (*historyCmd) {
                        *tempCmdCounter = *historyCmd;
                        tempCmdCounter++;
                        historyCmd++;
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
                            free(tempCmd);
                            return 0;
                        }
                    } else {
                        if (historyNumber <= 0 || historyNumber > history.count) {
                            fprintf(stderr, "%s: !%d: event not found\n", SHELL_NAME, historyNumber);
                            free(tempCmd);
                            return 0;
                        }
                        historyIndex = historyNumber - 1;
                    }

                    char *historyCmd = history.elements[historyIndex];
                    while (*historyCmd) {
                        *tempCmdCounter++ = *historyCmd++;
                    }
                } else {
                    cmdCounter -= isNegative ? 2 : 1;
                    fprintf(stderr, "%s: %s: event not found\n", SHELL_NAME, cmdCounter);
                    free(tempCmd);
                    return 0;
                }
            } else {
                *tempCmdCounter++ = *cmdCounter++;
            }
        }

        *tempCmdCounter = '\0';
        strcpy(cmd, tempCmd);
        free(tempCmd);
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
                processCommand(cmd);
            }
        }
        fclose(fp);
    } else {
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
                        if (strcmp(cmd, EXIT_COMMAND) == 0) {
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

        freeHistoryElements();
    }
    free(cmd);
    return 0;
}
