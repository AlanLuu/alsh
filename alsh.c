#include <ctype.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define COMMAND_BUFFER_SIZE 4096
#define COMMENT_CHAR '#'
#define CWD_BUFFER_SIZE 4096
#define EXIT_COMMAND "exit"
#define HISTORY_COMMAND "history"
#define HISTORY_FILE_NAME ".alsh_history"
#define HISTORY_MAX_ELEMENTS 100
#define SHELL_NAME "alsh"
#define SPLIT_ARR_MAX_ELEMENTS 100

static char cwd[CWD_BUFFER_SIZE]; //Current working directory

struct {
    char *elements[HISTORY_MAX_ELEMENTS];
    int count;
} history;
#define FREE_HISTORY_ELEMENTS do { \
    for (int i = 0; i < history.count; i++) { \
        free(history.elements[i]); \
    } \
} while (0)

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
 * Splits a string from the first occurrence of delim
 * Remember to free() the returned string
*/
char** split(char *str, char *delim) {
    char *token = strtok(str, delim);
    char **tokens = malloc(sizeof(char*) * SPLIT_ARR_MAX_ELEMENTS);
    int i = 0;
    while (token != NULL) {
        tokens[i++] = token;
        token = strtok(NULL, delim);
    }
    tokens[i] = NULL;
    return tokens;
}

/**
 * Trims whitespace from the beginning and end of a string
 * Returns false if the string is empty, true otherwise
*/
bool trimWhitespaceFromEnds(char *str) {
    size_t len = strlen(str);
    if (len == 0) return false;

    size_t i = 0;
    while (str[i] == ' ') {
        i++;
    }
    size_t j = len - 1;
    while (str[j] == ' ') {
        j--;
    }
    int k = 0;
    while (i <= j) {
        str[k] = str[i];
        i++;
        k++;
    }
    str[k] = '\0';
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

        if (*fileName == '\0') {
            fprintf(stderr, "%s: %s: Missing file name\n", SHELL_NAME, *fopenMode == 'a' ? ">>" : ">");
            status = malloc(sizeof(int));
            *status = -1;
        } else {
            int oldStdout = dup(STDOUT_FILENO);
            status = malloc(sizeof(int) * 2);
            *status = 1;
            status[1] = oldStdout;
            
            FILE *fp = fopen(fileName, fopenMode);
            dup2(fileno(fp), STDOUT_FILENO);
            fclose(fp);
        }
    } else {
        status = calloc(1, sizeof(int));
    }
    return status;
}

int* handleRedirectStdin(char *cmd) {
    char *stdinRedirectChr = strchr(cmd, '<');
    int *status;
    if (stdinRedirectChr != NULL) {
        char *tempCmd = malloc(sizeof(char) * COMMAND_BUFFER_SIZE);
        strcpy(tempCmd, cmd);

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
            status = malloc(sizeof(int));
            *status = -1;
        } else {
            int oldStdin = dup(STDIN_FILENO);
            dup2(fileno(fp), STDIN_FILENO);
            fclose(fp);
            status = malloc(sizeof(int) * 2);
            *status = 1;
            status[1] = oldStdin;
        }

        free(tempCmd);
    } else {
        status = calloc(1, sizeof(int));
    }
    return status;
}

void executeCommand(char *cmd) {
    char *tempCmd = malloc(sizeof(char) * COMMAND_BUFFER_SIZE);
    char *cmdPtr = cmd;
    char *tempCmdPtr = tempCmd;
    int cmdIndex = 0;
    while (*cmdPtr) {
        switch (*cmdPtr) {
            case '<':
            case '>':
                //Avoid ub if cmdPtr is at the beginning of the string
                if (cmdIndex > 0
                    && (*(cmdPtr - 1) != ' '
                        || (*(cmdPtr + 1) != ' ' && *(cmdPtr + 1) != '>')
                    )
                ) {
                    *tempCmdPtr++ = ' ';
                    *tempCmdPtr++ = *cmdPtr++;
                    if (*(cmdPtr - 1) == '>' && *cmdPtr == '>') {
                        *tempCmdPtr++ = *cmdPtr++;
                    }
                    *tempCmdPtr++ = ' ';
                } else {
                    *tempCmdPtr++ = *cmdPtr++;
                }
                break;
            default:
                *tempCmdPtr++ = *cmdPtr++;
                break;
        }
        cmdIndex++;
    }
    *tempCmdPtr = '\0';
    
    char **tokens = split(tempCmd, " ");

    //Add --color=auto to ls command
    if (strcmp(tokens[0], "ls") == 0) {
        int i = 0;
        while (tokens[i] != NULL) {
            i++;
        }
        tokens[i++] = "--color=auto";
        tokens[i] = NULL;
    }

    //cd command
    if (strcmp(tokens[0], "cd") == 0) {
        char *arg = tokens[1];
        if (arg == NULL) { //No argument, change to home directory
            if (chdir(getenv("HOME")) != 0) {
                //Should not happen
                fprintf(stderr, "%s: cd: Failed to change to home directory\n", SHELL_NAME);
            }
        } else if (strcmp(arg, "..") == 0) { //Go up one directory
            char *lastSlashPos = strrchr(cwd, '/');
            *(lastSlashPos + 1) = '\0';
            if (chdir(cwd) != 0) {
                //Should not happen
                fprintf(stderr, "%s: cd: Failed to change to parent directory\n", SHELL_NAME);
            }
        } else if (chdir(arg) != 0) { //Change to specified directory
            fprintf(stderr, "%s: cd: %s: No such file or directory\n", SHELL_NAME, arg);
        }
        free(tempCmd);
        free(tokens);
        return;
    }

    //history command
    if (strcmp(tokens[0], HISTORY_COMMAND) == 0) {
        char *flag = tokens[1];
        if (flag != NULL) {
            char flagChr = flag[1];
            switch (flagChr) {
                case 'c':
                    FREE_HISTORY_ELEMENTS;
                    history.count = 0;
                    break;
                case 'w': {
                    //Total of 52 characters for /home/<username>/.alsh_history
                    //Maximum of 32 characters for <username>
                    //20 characters for the rest of the path
                    char historyFile[52];
                    strcpy(historyFile, getenv("HOME"));
                    strcat(historyFile, "/" HISTORY_FILE_NAME);
                    FILE *historyfp = fopen(historyFile, "w");
                    if (historyfp == NULL) {
                        fprintf(stderr, "%s: %s: Failed to open history file\n", SHELL_NAME, HISTORY_COMMAND);
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
                    break;
            }
        } else {
            for (int i = 0; i < history.count; i++) {
                printf("    %d. %s\n", i + 1, history.elements[i]);
            }
        }
        free(tempCmd);
        free(tokens);
        return;
    }

    int *stdinStatus = handleRedirectStdin(cmd);
    if (*stdinStatus == -1) {
        free(stdinStatus);
        free(tempCmd);
        free(tokens);
        return;
    }
    int *stdoutStatus = handleRedirectStdout(cmd);
    if (*stdoutStatus == -1) {
        free(stdinStatus);
        free(stdoutStatus);
        free(tempCmd);
        free(tokens);
        return;
    }
    pid_t cid = fork();
    if (cid == 0) {
        char *strsToRemove[] = {"<", ">", ">>"};
        for (size_t i = 0; i < sizeof(strsToRemove) / sizeof(*strsToRemove); i++) {
            removeStrFromArrIfExists(tokens, strsToRemove[i]);
        }
        execvp(tokens[0], tokens);
        fprintf(stderr, "%s: command not found\n", tokens[0]);
        exit(1);
    }
    while (wait(NULL) > 0);
    if (*stdinStatus) {
        dup2(stdinStatus[1], STDIN_FILENO);
    }
    if (*stdoutStatus) {
        dup2(stdoutStatus[1], STDOUT_FILENO);
    }
    free(stdinStatus);
    free(stdoutStatus);
    free(tempCmd);
    free(tokens);
}

void executeCommandsAndPipes(char *cmd) {
    char *pipeChr = strchr(cmd, '|');
    if (pipeChr != NULL) {
        char *tempCmd = malloc(sizeof(char) * COMMAND_BUFFER_SIZE);
        strcpy(tempCmd, cmd);

        char **tokens = split(tempCmd, "|");
        int tokensCount = 0;
        while (tokens[tokensCount] != NULL) {
            trimWhitespaceFromEnds(tokens[tokensCount]);
            tokensCount++;
        }
        int terminal_stdin = dup(STDIN_FILENO);
        int terminal_stdout = dup(STDOUT_FILENO);
        int fd[2];
        int i;
        for (i = 0; i < tokensCount - 1; i++) {
            if (pipe(fd) != 0) {
                //Should not happen
                fprintf(stderr, "%s: Failed to create pipe\n", SHELL_NAME);
                free(tempCmd);
                free(tokens);
                return;
            }
            pid_t cid = fork();
            if (cid == 0) {
                dup2(fd[1], STDOUT_FILENO);
                close(fd[0]);
                executeCommand(tokens[i]);
                exit(0);
            }
            while (wait(NULL) > 0);
            dup2(fd[0], STDIN_FILENO);
            close(fd[1]);
        }
        executeCommand(tokens[i]);
        dup2(terminal_stdout, STDOUT_FILENO);
        dup2(terminal_stdin, STDIN_FILENO);
        free(tempCmd);
        free(tokens);
        return;
    }
    executeCommand(cmd);
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
        char *tempCmd = malloc(sizeof(char) * COMMAND_BUFFER_SIZE);
        strcpy(tempCmd, cmd);

        char **tokens = split(tempCmd, ";");
        for (int i = 0; tokens[i] != NULL; i++) {
            trimWhitespaceFromEnds(tokens[i]);
            executeCommandsAndPipes(tokens[i]);
        }
        free(tempCmd);
        free(tokens);
        return;
    }

    executeCommandsAndPipes(cmd);
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
        history.elements[HISTORY_MAX_ELEMENTS - 1] = malloc(sizeof(char) * COMMAND_BUFFER_SIZE);
        strcpy(history.elements[HISTORY_MAX_ELEMENTS - 1], cmd);
    } else {
        history.elements[history.count] = malloc(sizeof(char) * COMMAND_BUFFER_SIZE);
        strcpy(history.elements[history.count], cmd);
        history.count++;
    }
}

int processHistoryExclamations(char *cmd) {
    if (strchr(cmd, '!') != NULL) {
        char *tempCmd = malloc(sizeof(char) * COMMAND_BUFFER_SIZE);
        char *cmdCounter = cmd;
        char *tempCmdCounter = tempCmd;

        while (*cmdCounter) {
            if (*cmdCounter == '!') {
                size_t cmdCounterLength = strlen(cmdCounter);
                if (cmdCounterLength <= 1) { //Only ! in command
                    free(tempCmd);
                    return 0;
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
        fprintf(stderr, "Error getting current working directory, exiting shell...\n");
        exit(1);
    }
    bool isRootUser = getuid() == 0;
    if (isRootUser) {
        //Print red prompt
        printf("\033[1;31m%s-root:\033[1;34m%s\033[0m# ", SHELL_NAME, cwd);
    } else {
        //Print regular prompt
        printf("%s:\033[1;34m%s\033[0m$ ", SHELL_NAME, cwd);
    }
}

int main(int argc, char *argv[]) {
    char *cmd = malloc(sizeof(char) * COMMAND_BUFFER_SIZE);
    if (argc > 1) {
        FILE *fp = fopen(argv[1], "r");
        if (fp == NULL) {
            fprintf(stderr, "%s: %s: No such file or directory\n", SHELL_NAME, argv[1]);
            exit(1);
        }
        while (fgets(cmd, COMMAND_BUFFER_SIZE, fp) != NULL) {
            removeNewlineIfExists(cmd);
            bool trimSuccess = trimWhitespaceFromEnds(cmd);
            if (*cmd != COMMENT_CHAR && trimSuccess) {
                processCommand(cmd);
            }
        }
        fclose(fp);
    } else {
        //Total of 52 characters for /home/<username>/.alsh_history
        //Maximum of 32 characters for <username>
        //20 characters for the rest of the path
        char historyFile[52];
        strcpy(historyFile, getenv("HOME"));
        strcat(historyFile, "/" HISTORY_FILE_NAME);
        FILE *historyfp = fopen(historyFile, "r");
        if (historyfp != NULL) {
            char *historyLine = malloc(sizeof(char) * COMMAND_BUFFER_SIZE);
            while (fgets(historyLine, COMMAND_BUFFER_SIZE, historyfp) != NULL) {
                removeNewlineIfExists(historyLine);
                bool trimSuccess = trimWhitespaceFromEnds(historyLine);
                if (*historyLine != COMMENT_CHAR && trimSuccess) {
                    addCommandToHistory(historyLine);
                }
            }
            free(historyLine);
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
                if (trimSuccess) {
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

        FREE_HISTORY_ELEMENTS;
    }
    free(cmd);
    return 0;
}
