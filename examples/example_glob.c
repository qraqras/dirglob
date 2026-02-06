/**
 * @file example_glob.c
 * @brief Example usage of rbc_glob API
 */

#include "rbc/rbc.h"
#include <stdio.h>

int main(void)
{
       printf("===========================================\n");
       printf("rbc_glob Example\n");
       printf("===========================================\n\n");

       /* Example 1: Simple pattern */
       printf("Example 1: Simple pattern '*.c'\n");
       printf("-------------------------------------------\n");
       {
              const char *patterns[] = {"*.c"};
              rbc_glob_result_t result = {0};
              if (rbc_glob(patterns, 1, 0, ".", true, &result, NULL, NULL) == RBC_GLOB_SUCCESS)
              {
                     printf("Found %zu files:\n", result.count);
                     for (size_t i = 0; i < result.count; i++)
                     {
                            printf("  %s\n", result.paths[i]);
                     }
                     rbc_globfree(&result);
              }
       }
       printf("\n");

       /* Example 2: Multiple patterns */
       printf("Example 2: Multiple patterns\n");
       printf("-------------------------------------------\n");
       {
              const char *patterns[] = {"*.c", "*.h"};
              rbc_glob_result_t result = {0};
              if (rbc_glob(patterns, 2, 0, ".", true, &result, NULL, NULL) == RBC_GLOB_SUCCESS)
              {
                     printf("Found %zu files:\n", result.count);
                     for (size_t i = 0; i < result.count; i++)
                     {
                            printf("  %s\n", result.paths[i]);
                     }
                     rbc_globfree(&result);
              }
       }
       printf("\n");

       /* Example 3: Brace expansion */
       printf("Example 3: Brace expansion 'src/{core,utils}/*.c'\n");
       printf("-------------------------------------------\n");
       {
              const char *patterns[] = {"src/{core,utils}/*.c"};
              rbc_glob_result_t result = {0};
              if (rbc_glob(patterns, 1, 0, ".", true, &result, NULL, NULL) == RBC_GLOB_SUCCESS)
              {
                     printf("Found %zu files:\n", result.count);
                     for (size_t i = 0; i < result.count; i++)
                     {
                            printf("  %s\n", result.paths[i]);
                     }
                     rbc_globfree(&result);
              }
       }

       return 0;
}
