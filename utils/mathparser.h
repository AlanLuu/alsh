#ifndef ALSH_MATH_PARSER_
#define ALSH_MATH_PARSER_

#define MATH_PARSER_OK 0
#define MATH_PARSER_DIVIDE_ZERO 1
#define MATH_PARSER_UNEXPECTED_CHAR 2
#define MATH_PARSER_PARSE_ERROR 3

double MathParser_parse(char *expression, int *parseStatus);

#endif
