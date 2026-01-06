#include <stdio.h>
#include <dirent.h>

int main()
{
    DIR *d = opendir("tests/fixtures");
    if (!d)
        return 1;
    struct dirent *dir;
    while ((dir = readdir(d)) != NULL)
    {
        printf("%s\n", dir->d_name);
    }
    closedir(d);
    return 0;
}
