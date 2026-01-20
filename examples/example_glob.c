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
              char **files;
              size_t count;
              if (rbc_glob(patterns, 1, 0, ".", true, &files, &count, NULL))
              {
                     printf("Found %zu files:\n", count);
                     for (size_t i = 0; i < count; i++)
                     {
                            printf("  %s\n", files[i]);
                     }
                     rbc_glob_free(files, count, NULL);
              }
       }
       printf("\n");

       /* Example 2: Multiple patterns */
       printf("Example 2: Multiple patterns\n");
       printf("-------------------------------------------\n");
       {
              const char *patterns[] = {"*.c", "*.h"};
              char **files;
              size_t count;
              if (rbc_glob(patterns, 2, 0, ".", true, &files, &count, NULL))
              {
                     printf("Found %zu files:\n", count);
                     for (size_t i = 0; i < count; i++)
                     {
                            printf("  %s\n", files[i]);
                     }
                     rbc_glob_free(files, count, NULL);
              }
       }
       printf("\n");

       /* Example 3: Brace expansion */
       printf("Example 3: Brace expansion 'src/{core,utils}/*.c'\n");
       printf("-------------------------------------------\n");
       {
              const char *patterns[] = {"src/{core,utils}/*.c"};
              char **files;
              size_t count;
              if (rbc_glob(patterns, 1, 0, ".", true, &files, &count, NULL))
              {
                     printf("Found %zu files:\n", count);
                     for (size_t i = 0; i < count; i++)
                     {
                            printf("  %s\n", files[i]);
                     }
                     rbc_glob_free(files, count, NULL);
              }
       }

       return 0;
}
