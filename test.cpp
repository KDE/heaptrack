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
    return 0;
}
