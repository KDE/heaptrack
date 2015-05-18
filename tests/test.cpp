#include <cstdlib>
#include <cstdio>

#define HAVE_ALIGNED_ALLOC defined(_ISOC11_SOURCE)

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

void asdf()
{
    int* i = new int;
    printf("i in asdf: %p\n", (void*)i);
}

void bar()
{
    asdf();
}

void laaa()
{
    bar();
}

static Foo foo;

int main()
{
    Foo* f = new Foo;
    printf("new Foo: %p\n", (void*)f);
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
    cfree(buf);

#if HAVE_ALIGNED_ALLOC
    buf = aligned_alloc(16, 160);
    printf("aligned_alloc: %p\n", buf);
    free(buf);
#endif

    buf = valloc(32);
    printf("valloc: %p\n", buf);
    free(buf);

    posix_memalign(&buf, 16, 64);
    printf("posix_memalign: %p\n", buf);
    free(buf);

    for (int i = 0; i < 10; ++i) {
        laaa();
    }
    laaa();

    return 0;
}
