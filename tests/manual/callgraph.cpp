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

void recurse(int i)
{
    new char[2];
    bar();
    if (i) {
        recurse(--i);
    }
}

int main()
{
    asdf();
    foo();
    bar();
    recurse(5);
    return 0;
}
