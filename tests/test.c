#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

int main()
{
    int i;
    // make app deterministic
    srand(0);
    void* p = malloc(1);
    for (i = 0; i < 10000; ++i) {
        malloc(rand() % 1000);
        usleep(100);
    }
    printf("malloc: %p\n", p);
    free(p);
    return 0;
}
