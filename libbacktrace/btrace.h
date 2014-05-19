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
 * Walk up the stack getting the instruction pointers and stuffing them into your 
 *  array. We will skip the first <addrs_to_skip> count of addrs, and we will return
 *  the count of addrs we found. You can pass the addrs to btrace_resolve_addr() to
 *  get modules, function names, etc.
 */
int btrace_get(uintptr_t *addrs, size_t count_addrs, uint32_t addrs_to_skip);

/*
 * Get symbol information given an instruction pointer.
 */
#define BTRACE_RESOLVE_ADDR_GET_FILENAME   0x00000001
#define BTRACE_RESOLVE_ADDR_DEMANGLE_FUNC  0x00000002
bool btrace_resolve_addr(btrace_info *info, uintptr_t addr, uint32_t flags);

/*
 * Get debug filename (or NULL if not found).
 */
const char *btrace_get_debug_filename(const char *filename);

/*
 * Walk up the stack until we find a module that isn't the same as the one we're in.
 *  Return the full path string to that module. Will return NULL if nothing is found.
 */
const char *btrace_get_calling_module();
const char *btrace_get_current_module();

/*
 * Called when a new module is dlopen'd.
 */
void btrace_dlopen_notify(const char *filename);

int btrace_dump();
const char *btrace_demangle_function(const char *name, char *buffer, size_t buflen);

struct btrace_module_info
{
    //$ TODO mikesart: need an ID number in here. This will get incremented
    // everytime we have an address conflict. All stack traces should get this
    // ID so they know which module they should get symbols from.
    uintptr_t base_address;
    uint32_t address_size;
    struct backtrace_state *backtrace_state;
    const char *filename;
    int uuid_len;
    uint8_t uuid[20];
    int is_exe;
};

/*
 * Explicitly add a module to the module list. Used for symbol resolving.
 */
bool btrace_dlopen_add_module(const btrace_module_info &module_info);

/*
 * Uuid helper functions.
 */
int btrace_uuid_str_to_uuid(uint8_t uuid[20], const char *uuid_str);
void btrace_uuid_to_str(char uuid_str[41], const uint8_t *uuid, int len);

inline bool
operator <(const btrace_module_info &info1, const btrace_module_info &info2)
{
    if (info1.base_address < info2.base_address)
        return true;
    if(info1.base_address == info2.base_address)
    {
        if(info1.address_size < info2.address_size)
            return true;
        if (info1.address_size == info2.address_size)
        {
            if (strcmp(info1.filename, info2.filename) < 0)
                return false;
        }
    }
    return false;
}

inline bool
operator ==(const btrace_module_info &info1, const btrace_module_info &info2)
{
    return (info1.base_address == info2.base_address) &&
           (info1.address_size == info2.address_size) &&
            !strcmp(info1.filename, info2.filename);
}

inline bool
operator !=(const btrace_module_info &info1, const btrace_module_info &info2)
{
    return !(info1 == info2);
}

#endif // BTRACE_H
