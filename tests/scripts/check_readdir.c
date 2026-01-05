
#include <stdio.h>
#include <dirent.h>

int main(int argc, char **argv)
{
    DIR *dir = opendir(argv[1]);
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        printf("%s\n", entry->d_name);
    }
    closedir(dir);
    return 0;
}
