#include "mathparser.h"

#include "charlist.h"
#include <ctype.h>
#include "doublelist.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

bool isAnyOperator(char c) {
    return c == '+' || c == '-' || c == '*' || c == '/';
}

double parsePostfixExpr(char *postfixExpr, int *status) {
    DoubleList *output = DoubleList_create();
    CharList *temp = CharList_create();
    int numDecimalPoints = 0;
    char *postfixExprPtr = postfixExpr;
    while (*postfixExprPtr) {
        char token = *postfixExprPtr;
        if (isdigit(token) || token == '.') {
            if (token == '.') {
                numDecimalPoints++;
                if (numDecimalPoints > 1) {
                    *status = MATH_PARSER_PARSE_ERROR;
                    DoubleList_free(output);
                    CharList_free(temp);
                    return 0;
                }
            }
            CharList_add(temp, token);
            postfixExprPtr++;
        } else if (
            token == ' '
            && postfixExprPtr != postfixExpr
            && (isdigit(postfixExprPtr[-1]) || postfixExprPtr[-1] == '.')
        ) {
            numDecimalPoints--;
            char *numStr = CharList_toStr(temp);
            DoubleList_add(output, strtod(numStr, NULL));
            free(numStr);
            CharList_clear(temp);
            do {
                postfixExprPtr++;
            } while (*postfixExprPtr == ' ');
        } else if (isAnyOperator(token)) {
            double second = DoubleList_pop(output);
            if (output->size <= 0) {
                *status = MATH_PARSER_PARSE_ERROR;
                DoubleList_free(output);
                CharList_free(temp);
                return 0;
            }
            double first = DoubleList_pop(output);
            double result = 0.0;
            switch (token) {
                case '+':
                    result = first + second;
                    break;
                case '-':
                    result = first - second;
                    break;
                case '*':
                    result = first * second;
                    break;
                case '/':
                    if (second == 0) {
                        *status = MATH_PARSER_DIVIDE_ZERO;
                        DoubleList_free(output);
                        CharList_free(temp);
                        return 0;
                    }
                    result = first / second;
                    break;
            }
            DoubleList_add(output, result);
            postfixExprPtr++;
        } else {
            postfixExprPtr++;
        }
    }

    double result;
    if (temp->size > 0) {
        char *numStr = CharList_toStr(temp);
        result = strtod(numStr, NULL);
        free(numStr);
    } else {
        result = DoubleList_peek(output);
    }

    *status = MATH_PARSER_OK;
    DoubleList_free(output);
    CharList_free(temp);
    return result;
}

char* infixToPostfix(char *infixExpr) {
    CharList *output = CharList_create();
    CharList *stack = CharList_create();
    for (char *infixExprPtr = infixExpr; *infixExprPtr; infixExprPtr++) {
        char token = *infixExprPtr;
        if (token == ' ') continue;
        switch (token) {
            case '+':
            case '-': {
                CharList_add(output, ' ');
                char value = CharList_peek(stack);
                while (isAnyOperator(value)) {
                    CharList_pop(stack);
                    CharList_add(output, value);
                    CharList_add(output, ' ');
                    value = CharList_peek(stack);
                }
                CharList_add(stack, token);
                break;
            }
            case '*':
            case '/': {
                CharList_add(output, ' ');
                char value;
                while ((value = CharList_peek(stack)) == '*' || value == '/') {
                    CharList_pop(stack);
                    CharList_add(output, value);
                    CharList_add(output, ' ');
                }
                CharList_add(stack, token);
                break;
            }
            
            case '(':
                CharList_add(stack, token);
                break;
            case ')': {
                CharList_add(output, ' ');
                char value;
                while ((value = CharList_pop(stack)) != '(') {
                    CharList_add(output, value);
                }
                break;
            }
            
            default:
                if (!isdigit(token) && token != '.') {
                    CharList_free(output);
                    CharList_free(stack);
                    return NULL;
                }
                CharList_add(output, token);
                break;
        }
    }

    char remaining;
    while ((remaining = CharList_pop(stack))) {
        CharList_add(output, ' ');
        CharList_add(output, remaining);
    }

    char *result = CharList_toStr(output);
    CharList_free(output);
    CharList_free(stack);
    return result;
}

double MathParser_parse(char *expression, int *parseStatus) {
    char *postfixExpr = infixToPostfix(expression);
    if (postfixExpr == NULL) {
        if (parseStatus != NULL) {
            *parseStatus = MATH_PARSER_UNEXPECTED_CHAR;
        }
        return 0;
    }

    int status;
    double result = parsePostfixExpr(postfixExpr, &status);
    if (status != MATH_PARSER_OK) {
        if (parseStatus != NULL) {
            *parseStatus = status;
        }
        free(postfixExpr);
        return 0;
    }

    if (parseStatus != NULL) {
        *parseStatus = MATH_PARSER_OK;
    }
    free(postfixExpr);
    return result;
}
