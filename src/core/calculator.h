#pragma once

#include <stdbool.h>

#define CHRONVS_CALCULATOR_TEXT_SIZE 65

bool chronvs_calculator_evaluate(const char *expression, double *value,
                                 const char **error);
