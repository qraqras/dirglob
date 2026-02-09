#ifndef RBC_INTERNAL_UTILS_H
#define RBC_INTERNAL_UTILS_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "rbc/rbc.h"

char *rbc_strdup(const char *str);

uint32_t rbc_utf8_decode(const char **p);

#endif /* RBC_INTERNAL_UTILS_H */
