/**
 * @file string_set.h
 * @brief Simple string set for O(1) membership testing
 *
 * Used for brace expansion optimization.
 */

#ifndef RBC_STRING_SET_H
#define RBC_STRING_SET_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief String set structure
     *
     * Simple hash set for string membership testing.
     * Optimized for small sets (typical brace expansions).
     */
    typedef struct rbc_string_set_s rbc_string_set_t;

    /**
     * @brief Create a new string set
     *
     * @param expected_size Expected number of strings (hint for allocation)
     * @return New string set (must be freed with rbc_string_set_free)
     */
    rbc_string_set_t *rbc_string_set_create(size_t expected_size);

    /**
     * @brief Add a string to the set
     *
     * @param set String set
     * @param str String to add (will be copied)
     * @return true if added, false on error
     */
    bool rbc_string_set_add(rbc_string_set_t *set, const char *str);

    /**
     * @brief Add a string with specified length
     *
     * @param set String set
     * @param str String to add (not necessarily null-terminated)
     * @param len Length of string
     * @return true if added, false on error
     */
    bool rbc_string_set_add_n(rbc_string_set_t *set, const char *str, size_t len);

    /**
     * @brief Check if string is in the set
     *
     * @param set String set
     * @param str String to check
     * @return true if present, false otherwise
     */
    bool rbc_string_set_contains(const rbc_string_set_t *set, const char *str);

    /**
     * @brief Free string set
     *
     * @param set String set to free
     */
    void rbc_string_set_free(rbc_string_set_t *set);

#ifdef __cplusplus
}
#endif

#endif /* RBC_STRING_SET_H */
