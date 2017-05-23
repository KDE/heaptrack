#include <stdlib.h>

char* allocate_something(int size)
{
    return malloc(size);
}

char* foo()
{
    return allocate_something(100);
}

char* bar()
{
    return allocate_something(25);
}

int main()
{
    char* f1 = foo();
    char* b2 = bar();
    free(f1);
    char* b3 = bar();
    char* b4 = bar();
    free(b2);
    free(b3);
    free(b4);
    char* f2 = foo();
    free(f2);
    return 0;
}
