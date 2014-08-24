void asdf()
{
    new char[1];
}

void foo()
{
    asdf();
    new char[12];
}

void bar()
{
    asdf();
    foo();
    new char[123];
}

int main()
{
    asdf();
    foo();
    bar();
    return 0;
}
