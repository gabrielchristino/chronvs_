#include "core/calculator.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *cursor;
    const char *error;
    unsigned depth;
} expression_t;

static double sum(expression_t *expression);

static void skip_spaces(expression_t *expression) {
    while (*expression->cursor == ' ') ++expression->cursor;
}

static double atom(expression_t *expression) {
    skip_spaces(expression);
    if (++expression->depth > 8) {
        expression->error = "Expressao muito profunda";
        --expression->depth;
        return 0;
    }

    double value = 0;
    if (*expression->cursor == '+' || *expression->cursor == '-') {
        const char sign = *expression->cursor++;
        value = atom(expression);
        if (sign == '-') value = -value;
    } else if (*expression->cursor == '(') {
        ++expression->cursor;
        value = sum(expression);
        skip_spaces(expression);
        if (*expression->cursor == ')') ++expression->cursor;
        else expression->error = "Confira os parenteses";
    } else {
        const char *start = expression->cursor;
        bool digits = false;
        while (isdigit((unsigned char)*expression->cursor)) {
            ++expression->cursor;
            digits = true;
        }
        if (*expression->cursor == '.') {
            ++expression->cursor;
            while (isdigit((unsigned char)*expression->cursor)) {
                ++expression->cursor;
                digits = true;
            }
        }
        if (!digits) {
            expression->error = "Expressao incompleta";
        } else {
            if (*expression->cursor == 'e' || *expression->cursor == 'E') {
                ++expression->cursor;
                if (*expression->cursor == '+' || *expression->cursor == '-') {
                    ++expression->cursor;
                }
                if (!isdigit((unsigned char)*expression->cursor)) {
                    expression->error = "Numero invalido";
                }
                while (isdigit((unsigned char)*expression->cursor)) {
                    ++expression->cursor;
                }
            }
            char *end;
            value = strtod(start, &end);
            if (end != expression->cursor) expression->error = "Numero invalido";
        }
    }

    --expression->depth;
    if (!isfinite(value)) expression->error = "Resultado fora do limite";
    return value;
}

static double product(expression_t *expression) {
    double value = atom(expression);
    skip_spaces(expression);
    while (!expression->error &&
           (*expression->cursor == '*' || *expression->cursor == '/')) {
        const char operation = *expression->cursor++;
        const double right = atom(expression);
        if (operation == '/' && right == 0) {
            expression->error = "Divisao por zero";
            return 0;
        }
        value = operation == '*' ? value * right : value / right;
        skip_spaces(expression);
        if (!isfinite(value)) expression->error = "Resultado fora do limite";
    }
    return value;
}

static double sum(expression_t *expression) {
    double value = product(expression);
    skip_spaces(expression);
    while (!expression->error &&
           (*expression->cursor == '+' || *expression->cursor == '-')) {
        const char operation = *expression->cursor++;
        const double right = product(expression);
        value = operation == '+' ? value + right : value - right;
        skip_spaces(expression);
        if (!isfinite(value)) expression->error = "Resultado fora do limite";
    }
    return value;
}

bool chronvs_calculator_evaluate(const char *text, double *value,
                                 const char **error) {
    if (!text || !value || !error || strlen(text) >= CHRONVS_CALCULATOR_TEXT_SIZE) {
        if (error) *error = "Expressao muito longa";
        return false;
    }
    expression_t expression = {.cursor = text};
    *value = sum(&expression);
    skip_spaces(&expression);
    if (!expression.error && *expression.cursor) {
        expression.error = "Expressao invalida";
    }
    *error = expression.error;
    return expression.error == NULL;
}
