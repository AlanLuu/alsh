#include "mathparser.h"

#include "charlist.h"
#include <ctype.h>
#include "doublelist.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define MATH_PARSER_OK 0
#define MATH_PARSER_DIVIDE_ZERO 1
#define MATH_PARSER_UNEXPECTED_CHAR 2
#define MATH_PARSER_PARSE_ERROR 3

#define NEGATE_SYMBOL 'm'

bool MathParser_isAnyOperator(char c) {
    return c == '+' || c == '-' || c == '*' || c == '/';
}

bool MathParser_containsOperator(char *str) {
    char *strPtr = str;
    while (*strPtr) {
        if (MathParser_isAnyOperator(*strPtr++)) return true;
    }
    return false;
}

double parsePostfixExpr(char *postfixExpr, int *status) {
    DoubleList *output = DoubleList_create();
    CharList *temp = CharList_create();
    bool isPositive = true;
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
            double numStrVal = strtod(numStr, NULL);
            DoubleList_add(output, isPositive ? numStrVal : -numStrVal);
            isPositive = true;
            free(numStr);
            CharList_clear(temp);
            do {
                postfixExprPtr++;
            } while (*postfixExprPtr == ' ');
        } else if (MathParser_isAnyOperator(token)) {
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
        } else if (token == NEGATE_SYMBOL) {
            isPositive = !isPositive;
            postfixExprPtr++;
        } else {
            postfixExprPtr++;
        }
    }

    double result;
    if (temp->size > 0) {
        char *numStr = CharList_toStr(temp);
        result = strtod(numStr, NULL);
        result = isPositive ? result : -result;
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
    bool isBeginningOfExpr = true;
    bool prevTokenIsOperator = false;
    for (char *infixExprPtr = infixExpr; *infixExprPtr; infixExprPtr++) {
        bool isParentheses = false;
        char token = *infixExprPtr;
        if (token == ' ') continue;
        switch (token) {
            case '+':
            case '-': {
                if (token == '-' && (isBeginningOfExpr || prevTokenIsOperator)) {
                    CharList_add(output, NEGATE_SYMBOL);
                } else {
                    CharList_add(output, ' ');
                    char value = CharList_peek(stack);
                    while (MathParser_isAnyOperator(value)) {
                        CharList_pop(stack);
                        CharList_add(output, value);
                        CharList_add(output, ' ');
                        value = CharList_peek(stack);
                    }
                    CharList_add(stack, token);
                }
                prevTokenIsOperator = true;
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
                prevTokenIsOperator = true;
                break;
            }
            
            case '(':
                CharList_add(stack, token);
                isParentheses = true;
                break;
            case ')': {
                CharList_add(output, ' ');
                char value;
                while ((value = CharList_pop(stack)) != '(') {
                    CharList_add(output, value);
                }
                isParentheses = true;
                break;
            }
            
            default:
                if (!isdigit(token) && token != '.') {
                    CharList_free(output);
                    CharList_free(stack);
                    return NULL;
                }
                CharList_add(output, token);
                prevTokenIsOperator = false;
                break;
        }
        if (!isParentheses) isBeginningOfExpr = false;
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

bool MathParser_printErrMsg(int parseStatus, char *shellName) {
    if (parseStatus != MATH_PARSER_OK) {
        switch (parseStatus) {
            case MATH_PARSER_DIVIDE_ZERO:
                fprintf(stderr, "%s: Divison by 0 error\n", shellName);
                break;
            case MATH_PARSER_UNEXPECTED_CHAR:
                fprintf(stderr, "%s: Unexpected non-digit/non-decimal characters in math expression\n", shellName);
                break;
            case MATH_PARSER_PARSE_ERROR:
                fprintf(stderr, "%s: Math expression parse error\n", shellName);
                break;
        }
        return true;
    }
    return false;
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
