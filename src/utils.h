#ifndef RBC_INTERNAL_UTILS_H
#define RBC_INTERNAL_UTILS_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

char *rbc_strdup(const char *str);

uint32_t rbc_next_codepoint(const char **p);

#endif /* RBC_INTERNAL_UTILS_H */
