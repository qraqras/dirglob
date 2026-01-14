#include <stdio.h>
#include <rbc/rbc.h>

int main() {
    bool m1 = rbc_fnmatch("?", ".", 0);
    bool m2 = rbc_fnmatch("?", ".", RBC_FNM_DOTMATCH);
    printf("? matching . (no flags): %d\n", m1);
    printf("? matching . (dotmatch): %d\n", m2);
    return 0;
}
