inline void __attribute__((always_inline)) asdf()
{
    new char[1234];
}

inline void __attribute__((always_inline)) bar()
{
    asdf();
}

inline void __attribute__((always_inline)) foo()
{
    bar();
}

int main()
{
    foo();
}
