#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define COMMAND_BUFFER_SIZE 4096
#define COMMAND_MAX_TOKENS 100
#define SHELL_NAME "alsh"

/**
 * Remember to free() the returned string
*/
char* getCurrentWorkingDirectory() {
    int cwdSize = COMMAND_BUFFER_SIZE;
    char *cwd = malloc(sizeof(char) * cwdSize);
    getcwd(cwd, cwdSize);
    return cwd;
}

/**
 * Splits a string from the first occurence of delim
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
    int len = strlen(buffer);
    int i = 0;
    while (buffer[i] == ' ') {
        i++;
    }
    int j = len - 1;
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
    char *redirectChr = strchr(buffer, '>');
    int *status;
    if (redirectChr != NULL) {
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
    char *redirectChr = strchr(buffer, '<');
    int *status;
    if (redirectChr != NULL) {
        char *tempBuffer = malloc(sizeof(char) * COMMAND_BUFFER_SIZE);
        strcpy(tempBuffer, buffer);
        char **tokens = split(tempBuffer, "<");
        char *fileName = tokens[1];
        trimWhitespaceFromEnds(fileName);

        FILE *fp = fopen(fileName, "r");
        if (fp == NULL) {
            printf("%s: %s: No such file or directory\n", SHELL_NAME, fileName);
            status = malloc(sizeof(int));
            status[0] = -1;
        } else {
            int oldStdin = dup(STDIN_FILENO);
            dup2(fileno(fp), STDIN_FILENO);
            fclose(fp);
            status = malloc(sizeof(int) * 2);
            status[0] = true;
            status[1] = oldStdin;
        }
        free(tempBuffer);
        free(tokens);
    } else {
        status = calloc(1, sizeof(int));
    }
    return status;
}

/**
 * Removes the first occurence of str from tokens
*/
char** removeString(char **tokens, char *str) {
    char **newTokens = malloc(sizeof(char*) * COMMAND_MAX_TOKENS);
    int i = 0;
    while (tokens[i] != NULL) {
        if (strcmp(tokens[i], str) == 0) {
            newTokens[i] = NULL;
            break;
        }
        newTokens[i] = tokens[i];
        i++;
    }
    return newTokens;
}

void executeCommand(char *buffer) {
    char *tempBuffer = malloc(sizeof(char) * COMMAND_BUFFER_SIZE);
    strcpy(tempBuffer, buffer);
    char **tokens = split(tempBuffer, " ");

    if (strcmp(tokens[0], "cd") == 0) { //Change directory
        if (tokens[1] == NULL) { //No argument, change to home directory
            chdir(getenv("HOME"));
        } else if (strcmp(tokens[1], "..") == 0) { //Go up one directory
            char *cwd = getCurrentWorkingDirectory();
            char *lastSlash = strrchr(cwd, '/');
            *lastSlash = '\0'; //Remove last slash and everything after it
            chdir(cwd);
            free(cwd);
        } else if (chdir(tokens[1]) != 0) {
            printf("cd: %s: No such file or directory\n", tokens[1]);
        }
        free(tempBuffer);
        free(tokens);
        return;
    }
    int *stdInStatus = handleRedirectStdin(buffer);
    if (stdInStatus[0] == -1) {
        free(stdInStatus);
        free(tempBuffer);
        free(tokens);
        return;
    }
    int *stdOutStatus = handleRedirectStdout(buffer);
    pid_t cid = fork();
    if (cid == 0) {
        if (stdInStatus[0] || stdOutStatus[0]) {
            char **newTokens = removeString(tokens, stdInStatus[0] ? "<" : ">");
            execvp(newTokens[0], newTokens);
        } else {
            execvp(tokens[0], tokens);
        }
        printf("%s: command not found\n", tokens[0]);
        exit(0);
    } else {
        wait(NULL);
        if (stdInStatus[0]) {
            dup2(stdInStatus[1], STDIN_FILENO);
        }
        if (stdOutStatus[0]) {
            dup2(stdOutStatus[1], STDOUT_FILENO);
        }
        free(stdInStatus);
        free(stdOutStatus);
        free(tempBuffer);
        free(tokens);
    }
}

void processPrompt(char *buffer) {
    //Check for pipes
    char *pipeChr = strchr(buffer, '|');
    if (pipeChr != NULL) {
        char *tempBuffer = malloc(sizeof(char) * COMMAND_BUFFER_SIZE);
        strcpy(tempBuffer, buffer);
        char **tokens = split(tempBuffer, "|");

        int stdin_copy = dup(STDIN_FILENO);
        int stdout_copy = dup(STDOUT_FILENO);
        int fd[2];
        pipe(fd);
        pid_t cid = fork();
        if (cid == 0) {
            dup2(fd[1], STDOUT_FILENO);
            close(fd[0]);
            close(fd[1]);
            executeCommand(tokens[0]);
            exit(0);
        } else {
            dup2(fd[0], STDIN_FILENO);
            close(fd[0]);
            close(fd[1]);
            executeCommand(tokens[1]);
            wait(NULL);
        }
        dup2(stdin_copy, STDIN_FILENO);
        dup2(stdout_copy, STDOUT_FILENO);
        free(tempBuffer);
        free(tokens);
        return;
    }
    executeCommand(buffer);
}

void printIntro() {
    printf("Welcome to %s!\n", SHELL_NAME);
    printf("Type 'exit' to exit.\n\n");
}

void printPrompt() {
    char *cwd = getCurrentWorkingDirectory();
    printf("%s:%s:%s> ", SHELL_NAME, cwd, getuid() == 0 ? "#" : "$");
    free(cwd);
}

void removeNewlineIfExists(char *buffer) {
    int len = strlen(buffer);
    if (buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
    }
}

int main(int argc, char *argv[]) {
    char buffer[COMMAND_BUFFER_SIZE];
    if (argc > 1) {
        FILE *fp = fopen(argv[1], "r");
        if (fp == NULL) {
            printf("%s: %s: No such file or directory\n", SHELL_NAME, argv[1]);
            exit(1);
        }
        while (fgets(buffer, COMMAND_BUFFER_SIZE, fp) != NULL) {
            removeNewlineIfExists(buffer);
            if (strlen(buffer) > 0) processPrompt(buffer);
        }
        fclose(fp);
    } else {
        bool stdinFromTerminal = isatty(STDIN_FILENO);
        if (stdinFromTerminal) {
            printIntro();
            printPrompt();
        }
        while (fgets(buffer, COMMAND_BUFFER_SIZE, stdin) != NULL) {
            removeNewlineIfExists(buffer);
            if (strcmp(buffer, "exit") == 0) break;
            if (strlen(buffer) > 0) processPrompt(buffer);
            if (stdinFromTerminal) printPrompt();
        }
        if (stdinFromTerminal) printf("\n");
    }
    return 0;
}