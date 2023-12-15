#ifndef ALSH_MATH_PARSER_
#define ALSH_MATH_PARSER_

#include <stdbool.h>

bool MathParser_containsOperator(char *str);
bool MathParser_isAnyOperator(char c);
double MathParser_parse(char *expression, int *parseStatus);
bool MathParser_printErrMsg(int parseStatus, char *shellName);

#endif
