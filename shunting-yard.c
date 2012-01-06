/*
 * Copyright 2011, 2012 Brian Marshall. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     1. Redistributions of source code must retain the above copyright notice,
 *        this list of conditions and the following disclaimer.
 *     2. Redistributions in binary form must reproduce the above copyright
 *        notice, this list of conditions and the following disclaimer in the
 *        documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <float.h>
#include <ctype.h>
#include <errno.h>
#include "config.h"
#include "stack.h"
#include "shunting-yard.h"

const int op_order_len = 4;
const char *op_order[] = {"^", "*/", "+-", "("};

int main(int argc, char *argv[]) {
    char *str = join_argv(argc, argv);
    double result = shunting_yard(str);
    free(str);

    if (errno != SUCCESS)
        return EXIT_FAILURE;

    char *result_str = trim_double(result);
    printf("%s\n", result_str);
    free(result_str);

    return EXIT_SUCCESS;
}

/**
 * Concatenate all the arguments passed to the program.
 */
char *join_argv(int count, char *src[]) {
    /* Allocate a buffer for the full string */
    int len = 0;
    for (int i = 0; i < count; ++i)
        len += strlen(src[i]) + 1;

    /* Concatenate the arguments */
    char *str = calloc(count, len + 1);
    for (int i = 1; i < count; ++i) {
        if (i > 1) strcat(str, " ");
        strcat(str, src[i]);
    }

    return str;
}

/**
 * Parse a string and do the calculations. In the event of an error, will set
 * errno to an error code and return zero.
 */
double shunting_yard(char *str) {
    double result;
    stack *operands = stack_alloc();
    stack *operators = stack_alloc();
    stack_item *item;

    /* Loop through expression */
    int token_pos = -1;
    int paren_depth = 0;
    int paren_pos = -1; /* only used for error reporting */
    char *operand;
    char prev_chr = '\0';
    for (int i = 0; i <= strlen(str); ++i) {
        if (str[i] == ' ') continue;
        char chr_str[] = {str[i], '\0'};   /* convert char to char* */

        /* Operands */
        if (is_operand(str[i])) {
            if (token_pos == -1) token_pos = i;
            goto skip;
        } else if (token_pos != -1) { /* end of operand */
            operand = rtrim(substr(str, token_pos, i - token_pos));

            /* Syntax check. Error if one of the following is true:
             *     1. Operand ONLY contains "."
             *     2. Operand contains a space
             *     3. Operand contains more than one "."
             */
            if (strcmp(operand, ".") == 0 || strchr(operand, ' ') != NULL
                    || strchr(operand, '.') != strrchr(operand, '.')) {
                error(ERROR_SYNTAX_OPERAND, token_pos, str);
                goto exit;
            }

            stack_push_unalloc(operands, operand);
            token_pos = -1;
        }

        /* Operators */
        if (is_operator(str[i])) {
            /* Apply one operator already on the stack if:
             *     1. It's of higher precedence
             *     2. The current operator either isn't unary, or is unary and
             *        the previous operator is unary too
             */

            bool unary = is_unary(str[i], prev_chr);
            if (!stack_is_empty(operators)
                    && ((unary && stack_top_item(operators)->flags) || !unary)
                    && compare_operators(stack_top(operators), chr_str)) {
                item = stack_pop_item(operators);
                if (!apply_operator(item->val[0], item->flags, operands)) {
                    error(ERROR_SYNTAX, i, str);
                    goto exit;
                }
                stack_free_item(item);
            }

            stack_push(operators, chr_str, unary);
        }
        /* Parentheses */
        else if (str[i] == '(') {
            stack_push(operators, chr_str, 0);
            ++paren_depth;
            if (paren_depth == 1) paren_pos = i;
        } else if (str[i] == ')') {
            if (!paren_depth) {
                error(ERROR_RIGHT_PAREN, i, str);
                goto exit;
            }

            /* Pop and apply operators until we reach the left paren */
            while (!stack_is_empty(operators)) {
                if (stack_top(operators)[0] == '(') {
                    stack_pop_char(operators);
                    --paren_depth;
                    break;
                }

                item = stack_pop_item(operators);
                if (!apply_operator(item->val[0], item->flags, operands)) {
                    /* TODO: accurate column number (currently is just the col
                     * num of the right paren) */
                    error(ERROR_SYNTAX, i, str);
                    goto exit;
                }
                stack_free_item(item);
            }
        }
        /* Unknown character */
        else if (str[i] != '\0' && str[i] != '\n') {
            error(ERROR_UNRECOGNIZED, i, str);
            goto exit;
        }

skip:
        if (str[i] == '\n') break;
        prev_chr = str[i];
    }

    if (paren_depth) {
        error(ERROR_LEFT_PAREN, paren_pos, str);
        goto exit;
    }

    /* End of string - apply any remaining operators on the stack */
    while (!stack_is_empty(operators)) {
        item = stack_pop_item(operators);
        if (!apply_operator(item->val[0], item->flags, operands)) {
            error(ERROR_SYNTAX_STACK, NO_COL_NUM, str);
            goto exit;
        }
        stack_free_item(item);
    }

    /* Save the final result */
    if (stack_is_empty(operands))
        error(ERROR_NO_INPUT, NO_COL_NUM, str);
    else
        result = strtod_unalloc(stack_pop(operands));

    /* Free memory and return */
exit:
    stack_free(operands);
    stack_free(operators);
    return result;
}

/**
 * Apply an operator to the top 2 operands on the stack.
 *
 * @param operator Operator to use (e.g., +, -, /, *).
 * @param operands Operands stack.
 * @return true on success, false on failure.
 */
bool apply_operator(char operator, bool unary, stack *operands) {
    /* Check for underflow, as it indicates a syntax error */
    if (stack_is_empty(operands))
        return false;

    double result;
    double val2 = strtod_unalloc(stack_pop(operands));

    /* Handle unary operators */
    if (unary) {
        switch (operator) {
            case '+':
                /* values are already assumed positive */
                stack_push_unalloc(operands, num_to_str(val2));
                return true;
            case '-':
                result = -val2;
                stack_push_unalloc(operands, num_to_str(result));
                return true;
            case '!':
                result = tgamma(val2 + 1);
                stack_push_unalloc(operands, num_to_str(result));
                return true;
        }

        return false;   /* unknown operator */
    }

    /* Check for underflow again before we pop another operand */
    if (stack_is_empty(operands))
        return false;

    double val1 = strtod_unalloc(stack_pop(operands));
    switch (operator) {
        case '+': result = val1 + val2; break;
        case '-': result = val1 - val2; break;
        case '*': result = val1 * val2; break;
        case '/': result = val1 / val2; break;
        case '^': result = pow(val1, val2); break;
    }
    stack_push_unalloc(operands, num_to_str(result));

    return true;
}

/**
 * Compares the precedence of two operators.
 *
 * @param op1 First operator.
 * @param op2 Second operator.
 * @return 0 for the first operator, 1 for the second.
 */
int compare_operators(char *op1, char *op2) {
    int op1_rank = -1;
    int op2_rank = -1;

    /* Loop through operator order and compare */
    for (int i = 0; i < op_order_len; ++i) {
        if (strpbrk(op1, op_order[i])) op1_rank = i;
        if (strpbrk(op2, op_order[i])) op2_rank = i;
    }

    return op1_rank < op2_rank;
}

/**
 * Convert a number to a character string, for adding to the stack.
 */
char *num_to_str(double num) {
    char *str = malloc(DOUBLE_STR_LEN);
    snprintf(str, DOUBLE_STR_LEN, "%a", num);

    return str;
}

/**
 * Wrapper around strtod() that also frees the string that was converted.
 */
double strtod_unalloc(char *str) {
    double num = strtod(str, NULL);
    free(str);
    return num;
}

/**
 * Outputs an error.
 */
void error(int type, int col_num, char *str) {
    errno = type;

    char error_str[TERM_WIDTH] = "Error: ";
    switch (type) {
        case ERROR_SYNTAX:
        case ERROR_SYNTAX_STACK:
        case ERROR_SYNTAX_OPERAND:
            strcat(error_str, "malformed expression");
            break;
        case ERROR_RIGHT_PAREN:
            strcat(error_str, "mismatched right parenthesis");
            break;
        case ERROR_LEFT_PAREN:
            strcat(error_str, "mismatched (unclosed) left parenthesis");
            break;
        case ERROR_UNRECOGNIZED:
            strcat(error_str, "unrecognized character");
            break;
        case ERROR_NO_INPUT:
            fprintf(stderr, "This is a calculator - provide some math!\n");
            return;
        default:
            strcat(error_str, "unknown error");
    }

    /* Output excerpt and column marker */
    if (col_num != NO_COL_NUM) {
        strcat(error_str, ": ");

        ++col_num;  /* width variables below start at 1, so this should too */
        int total_width = TERM_WIDTH;
        int msg_width = (int)strlen(error_str);
        int avail_width = total_width - msg_width;
        int substr_start = MAX(col_num - avail_width / 2, 0);

        char *excerpt = substr(str, substr_start, avail_width);
        fprintf(stderr, "%s%s\n", error_str, excerpt);
        fprintf(stderr, "%*c\n", msg_width + col_num - substr_start, '^');
        free(excerpt);
    } else
        fprintf(stderr, "%s\n", error_str);
}

/**
 * Return a substring.
 */
char *substr(char *str, int start, int len) {
    char *substr = malloc(len + 1);
    memcpy(substr, str + start, len);
    substr[len] = '\0';

    return substr;
}

/**
 * Check if an operator is unary.
 */
bool is_unary(char operator, char prev_chr) {
    /* Special case for postfix unary operators */
    if (prev_chr == '!' && operator != '!')
        return false;

    /* Right paren counts as an operand for this check */
    return is_operator(prev_chr) || prev_chr == '\0' || ((is_operand(prev_chr)
                || prev_chr == ')') && operator == '!');
}

/**
 * Remove trailing zeroes from a double and return it as a string.
 */
char *trim_double(double num) {
    char *str = malloc(DECIMAL_DIG + 1);
    snprintf(str, DECIMAL_DIG + 1,
            num >= pow(10, MIN_E_DIGITS) ? "%.*e" : "%.*f", MIN_E_DIGITS, num);

    for (int i = strlen(str) - 1; i > 0; --i) {
        if (str[i] == '.') str[i] = '\0';
        if (str[i] != '0') break;

        str[i] = '\0';
    }

    return str;
}

/**
 * Trim whitespace from the end of a string.
 */
char *rtrim(char *str)
{
    char *end = str + strlen(str);
    while (isspace(*--end));
    *(end + 1) = '\0';
    return str;
}
