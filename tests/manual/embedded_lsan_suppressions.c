/*
    SPDX-FileCopyrightText: 2021 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include <stdint.h>
#include <stdlib.h>

// see upstream "documentation" at:
// https://github.com/llvm-mirror/compiler-rt/blob/master/include/sanitizer/lsan_interface.h#L76
__attribute__((used)) const char* __lsan_default_suppressions()
{
    return "leak:foobar\nleak:^leak*Supp$\n";
}

inline void escape(void* p)
{
    asm volatile("" : : "g"(p) : "memory");
}

__attribute__((noinline)) void* foobar()
{
    void* ptr = malloc(1);
    escape(ptr);
    return ptr;
}

__attribute__((noinline)) void* leak()
{
    void* ptr = malloc(2);
    escape(ptr);
    return ptr;
}

__attribute__((noinline)) void* leakFoo()
{
    void* ptr = malloc(3);
    escape(ptr);
    return ptr;
}

__attribute__((noinline)) void* leakFooSupp()
{
    void* ptr = malloc(4);
    escape(ptr);
    return ptr;
}

int main()
{
    foobar();
    leak();
    leakFoo();
    leakFooSupp();
    return 0;
}
