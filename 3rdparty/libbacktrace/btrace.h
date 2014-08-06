/**************************************************************************
 *
 * Copyright 2013-2014 RAD Game Tools and Valve Software
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 **************************************************************************/

#ifndef BTRACE_H
#define BTRACE_H

#include <stdint.h>
#include <string.h>

struct btrace_info
{
    uintptr_t addr;
    uintptr_t offset;
    const char *module;   // Guaranteed to be !NULL.
    const char *function; //
    const char *filename; //
    int linenumber;
    char demangled_func_buf[512];
};

/*
 * Get symbol information given an instruction pointer.
 */
enum ResolveFlags {
    None = 0x0,
    GET_FILENAME = 1 << 0,
    DEMANGLE_FUNC = 1 << 1
};
bool btrace_resolve_addr(btrace_info *info, uintptr_t addr, ResolveFlags flags);

/*
 * Called when a new module is dlopen'd.
 */
void btrace_dlopen_notify(const char *filename);

const char *btrace_demangle_function(const char *name, char *buffer, size_t buflen);

#endif // BTRACE_H
