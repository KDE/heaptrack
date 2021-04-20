/*
 * Copyright 2021 Milian Wolff <mail@milianw.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
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
