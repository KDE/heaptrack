#include <cstdlib>
#include <cstdio>

struct Foo {
    Foo()
        : i(new int)
    {
    }
    ~Foo()
    {
        delete i;
    }
    int* i;
};

static Foo foo;

int main()
{
    Foo* f = new Foo;
    printf("new Foo: %p\n", f);
    delete f;

    char* c = new char[1000];
    printf("new char[]: %p\n", c);
    delete[] c;

    void* buf = malloc(100);
    printf("malloc: %p\n", buf);
    buf = realloc(buf, 200);
    printf("realloc: %p\n", buf);
    free(buf);

    buf = calloc(5, 5);
    printf("calloc: %p\n", buf);
    free(buf);

    return 0;
}
