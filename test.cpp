#include <cstdlib>

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
    char* c = new char[1000];
    delete[] c;
    Foo* f = new Foo;
    delete f;

    void* buf = malloc(100);
    buf = realloc(buf, 200);
    free(buf);
    return 0;
}
