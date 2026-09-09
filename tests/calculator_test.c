#include "core/calculator.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static void valid(const char *expression, double expected) {
    double value;
    const char *error;
    assert(chronvs_calculator_evaluate(expression, &value, &error));
    assert(error == NULL);
    assert(fabs(value - expected) <= 1e-10 * fmax(1, fabs(expected)));
}

static void invalid(const char *expression) {
    double value;
    const char *error;
    assert(!chronvs_calculator_evaluate(expression, &value, &error));
    assert(error != NULL);
}

int main(void) {
    valid("2+3*4", 14);
    valid("(2+3)*4", 20);
    valid("-2*-3", 6);
    valid(".5+1.25", 1.75);
    valid("1/8", .125);
    valid("2--3", 5);
    valid(" 1 + 2 ", 3);
    valid("0.1+0.2", .3);

    const char *bad[] = {
        "", ".", "1/0", "1/(2-2)", "2+", "(2+3", "2(3)",
        "0x10", "nan", "inf", "1e999", "1e", "1e308*10",
        "1e308+1e308", "(((((((((1)))))))))", "---------1", "1..2"
    };
    for (unsigned index = 0; index < sizeof(bad) / sizeof(bad[0]); ++index) {
        invalid(bad[index]);
    }
    for (int left = -12; left <= 12; ++left) {
        for (int right = 1; right <= 12; ++right) {
            char expression[64];
            snprintf(expression, sizeof(expression), "(%d+%d)*%d/%d",
                     left, right, right, right);
            valid(expression, left + right);
        }
    }
    puts("Calculator passed: precedence, parentheses, unary signs, errors and 300 generated expressions.");
    return 0;
}
