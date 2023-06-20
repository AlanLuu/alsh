#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define COMMAND_BUFFER_SIZE 4096
#define COMMAND_MAX_TOKENS 100
#define COMMENT_CHAR '#'
#define EXIT_COMMAND "exit"
#define SHELL_NAME "alsh"

char cwd[COMMAND_BUFFER_SIZE]; //Current working directory

/**
 * Removes the newline character from the end of a string if it exists
*/
void removeNewlineIfExists(char *buffer) {
    size_t len = strlen(buffer);
    if (buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
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
char** split(char *buffer, char *delim) {
    char *token = strtok(buffer, delim);
    char **tokens = malloc(sizeof(char*) * COMMAND_MAX_TOKENS);
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
*/
void trimWhitespaceFromEnds(char *buffer) {
    size_t len = strlen(buffer);
    if (len == 0) return;

    size_t i = 0;
    while (buffer[i] == ' ') {
        i++;
    }
    size_t j = len - 1;
    while (buffer[j] == ' ') {
        j--;
    }
    int k = 0;
    while (i <= j) {
        buffer[k] = buffer[i];
        i++;
        k++;
    }
    buffer[k] = '\0';
}

int* handleRedirectStdout(char *buffer) {
    char *stdoutRedirectChr = strchr(buffer, '>');
    int *status;
    if (stdoutRedirectChr != NULL) {
        int oldStdout = dup(STDOUT_FILENO);
        status = malloc(sizeof(int) * 2);
        status[0] = true;
        status[1] = oldStdout;

        char *tempBuffer = malloc(sizeof(char) * COMMAND_BUFFER_SIZE);
        strcpy(tempBuffer, buffer);
        char **tokens = split(tempBuffer, ">");
        char *filename = tokens[1];
        trimWhitespaceFromEnds(filename);

        FILE *fp = fopen(filename, "w");
        dup2(fileno(fp), STDOUT_FILENO);
        fclose(fp);
        free(tempBuffer);
        free(tokens);
    } else {
        status = calloc(1, sizeof(int));
    }
    return status;
}

int* handleRedirectStdin(char *buffer) {
    char *stdinRedirectChr = strchr(buffer, '<');
    int *status;
    if (stdinRedirectChr != NULL) {
        char *tempBuffer = malloc(sizeof(char) * COMMAND_BUFFER_SIZE);
        strcpy(tempBuffer, buffer);
        char **tokens = split(tempBuffer, "<");
        char *fileName = tokens[1];
        trimWhitespaceFromEnds(fileName);

        FILE *fp = fopen(fileName, "r");
        if (fp == NULL) {
            char *stdoutRedirectChr = strchr(buffer, '>');
            status = malloc(sizeof(int));
            if (stdoutRedirectChr != NULL) {
                handleRedirectStdout(buffer);
                *status = true;
            } else {
                printf("%s: %s: No such file or directory\n", SHELL_NAME, fileName);
                *status = -1;
            }
        } else {
            int oldStdin = dup(STDIN_FILENO);
            dup2(fileno(fp), STDIN_FILENO);
            fclose(fp);
            status = malloc(sizeof(int) * 2);
            *status = true;
            status[1] = oldStdin;
        }
        free(tempBuffer);
        free(tokens);
    } else {
        status = calloc(1, sizeof(int));
    }
    return status;
}

void executeCommand(char *buffer) {
    char *tempBuffer = malloc(sizeof(char) * COMMAND_BUFFER_SIZE);
    strcpy(tempBuffer, buffer);
    char **tokens = split(tempBuffer, " ");

    //Add --color=auto to ls command
    if (strcmp(tokens[0], "ls") == 0) {
        int i = 0;
        while (tokens[i] != NULL) {
            i++;
        }
        tokens[i++] = "--color=auto";
        tokens[i] = NULL;
    }

    //Implement cd command
    if (strcmp(tokens[0], "cd") == 0) {
        char *arg = tokens[1];
        if (arg == NULL) { //No argument, change to home directory
            chdir(getenv("HOME"));
        } else if (strcmp(arg, "..") == 0) { //Go up one directory
            char *lastSlashPos = strrchr(cwd, '/');
            *(lastSlashPos + 1) = '\0';
            chdir(cwd);
        } else if (chdir(arg) != 0) { //Change to specified directory
            printf("cd: %s: No such file or directory\n", arg);
        }
        free(tempBuffer);
        free(tokens);
        return;
    }

    int *stdinStatus = handleRedirectStdin(buffer);
    if (*stdinStatus == -1) {
        free(stdinStatus);
        free(tempBuffer);
        free(tokens);
        return;
    }
    int *stdoutStatus = handleRedirectStdout(buffer);
    pid_t cid = fork();
    if (cid == 0) {
        removeStrFromArrIfExists(tokens, "<");
        removeStrFromArrIfExists(tokens, ">");
        execvp(tokens[0], tokens);
        printf("%s: command not found\n", tokens[0]);
        exit(1);
    }
    wait(NULL);
    if (*stdinStatus) {
        dup2(stdinStatus[1], STDIN_FILENO);
    }
    if (*stdoutStatus) {
        dup2(stdoutStatus[1], STDOUT_FILENO);
    }
    free(stdinStatus);
    free(stdoutStatus);
    free(tempBuffer);
    free(tokens);
}

void executeCommandsAndPipes(char *buffer) {
    char *pipeChr = strchr(buffer, '|');
    if (pipeChr != NULL) {
        char *tempBuffer = malloc(sizeof(char) * COMMAND_BUFFER_SIZE);
        strcpy(tempBuffer, buffer);

        char **tokens = split(tempBuffer, "|");
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
            pipe(fd);
            pid_t cid = fork();
            if (cid == 0) {
                dup2(fd[1], STDOUT_FILENO);
                close(fd[0]);
                executeCommand(tokens[i]);
                exit(0);
            }
            wait(NULL);
            dup2(fd[0], STDIN_FILENO);
            close(fd[1]);
        }
        executeCommand(tokens[i]);
        dup2(terminal_stdout, STDOUT_FILENO);
        dup2(terminal_stdin, STDIN_FILENO);
        free(tempBuffer);
        free(tokens);
        return;
    }
    executeCommand(buffer);
}

void processPrompt(char *buffer) {
    //Check for comments
    char *commentChr = strchr(buffer, COMMENT_CHAR);
    if (commentChr != NULL && *(commentChr - 1) == ' ') {
        *commentChr = '\0';
    }

    //Check for semicolon operators
    char *semicolonChr = strchr(buffer, ';');
    if (semicolonChr != NULL) {
        char *tempBuffer = malloc(sizeof(char) * COMMAND_BUFFER_SIZE);
        strcpy(tempBuffer, buffer);

        char **tokens = split(tempBuffer, ";");
        for (int i = 0; tokens[i] != NULL; i++) {
            trimWhitespaceFromEnds(tokens[i]);
            executeCommandsAndPipes(tokens[i]);
        }
        free(tempBuffer);
        free(tokens);
        return;
    }

    executeCommandsAndPipes(buffer);
}

void printIntro(void) {
    printf("Welcome to %s!\n", SHELL_NAME);
    printf("Type '%s' to exit.\n\n", EXIT_COMMAND);
}

void printPrompt(void) {
    if (getcwd(cwd, COMMAND_BUFFER_SIZE) == NULL) {
        printf("Error getting current working directory, exiting shell...\n");
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
    char *buffer = malloc(sizeof(char) * COMMAND_BUFFER_SIZE);
    if (argc > 1) {
        FILE *fp = fopen(argv[1], "r");
        if (fp == NULL) {
            printf("%s: %s: No such file or directory\n", SHELL_NAME, argv[1]);
            exit(1);
        }
        while (fgets(buffer, COMMAND_BUFFER_SIZE, fp) != NULL) {
            removeNewlineIfExists(buffer);
            trimWhitespaceFromEnds(buffer);
            if (*buffer != COMMENT_CHAR && strlen(buffer) > 0) {
                processPrompt(buffer);
            }
        }
        fclose(fp);
    } else {
        bool stdinFromTerminal = isatty(STDIN_FILENO);
        bool typedExitCommand = false;
        if (stdinFromTerminal) {
            printIntro();
            printPrompt();
        }
        while (fgets(buffer, COMMAND_BUFFER_SIZE, stdin) != NULL) {
            removeNewlineIfExists(buffer);
            trimWhitespaceFromEnds(buffer);
            if (*buffer != COMMENT_CHAR && strlen(buffer) > 0) {
                if (strcmp(buffer, EXIT_COMMAND) == 0) {
                    typedExitCommand = true;
                    break;
                }
                processPrompt(buffer);
            }
            if (stdinFromTerminal) printPrompt();
        }
        if (stdinFromTerminal && !typedExitCommand) printf("\n");
    }
    free(buffer);
    return 0;
}