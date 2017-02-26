void foo()
{
    new char[1];
}

void bar()
{
    foo(); new char[2];
}

void asdf()
{
    bar(); new char[3];
}

void foobar()
{
    asdf(); new char[5];
}

int main()
{
    asdf(); new char[4]; foobar();
    return 0;
}
