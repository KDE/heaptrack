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

//
// btrace.cpp
//
#include <inttypes.h>

#include <stdio.h>
#include <string.h>
#include <execinfo.h>
#include <link.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <cassert>

#include <mutex>
#include <vector>
#include <algorithm>

// We need local unwinding only (should be more optimal than local + remote).
#define UNW_LOCAL_ONLY
#include <libunwind.h>

#include "btrace.h"
#include "backtrace.h"

// our demangle routine from libelftc_dem_gnu3.c (from libcxxrt)
extern "C" char * __cxa_demangle_gnu3(const char *org);

#ifdef HAVE_UNW_BACKTRACE_SKIP
// fast(er) backtrace routine from libunwind
extern "C" int unw_backtrace_skip (void **buffer, int size, int skip);
#endif

// Our dlopen mutex to protect g_module_infos operations
static std::mutex &get_dlopen_mutex()
{
    static std::mutex g_dlopen_mutex;
    return g_dlopen_mutex;
}

// An array of all the modules that have been loaded in this process.
static std::vector<btrace_module_info> &get_module_infos()
{
    static std::vector<btrace_module_info> s_module_infos;
    return s_module_infos;
}

static void btrace_dlopen_notify_impl();

int
btrace_get(uintptr_t *addrs, size_t count_addrs, uint32_t addrs_to_skip)
{
#ifdef HAVE_UNW_BACKTRACE_SKIP
    return unw_backtrace_skip((void **)addrs, (int)count_addrs, addrs_to_skip);
#else
    size_t count = 0;
    unw_cursor_t cursor;
    unw_context_t context;

    unw_getcontext(&context);
    unw_init_local(&cursor, &context);

    while (count < count_addrs)
    {
        unw_word_t addr;

        if (unw_step(&cursor) <= 0)
            break;

        unw_get_reg(&cursor, UNW_REG_IP, &addr);
        // Could retrieve registers via something like this:
        //   unw_get_reg(&cursor, UNW_X86_EAX, &eax), ...

        if (addrs_to_skip)
        {
            addrs_to_skip--;
            continue;
        }

        addrs[count++] = (uintptr_t)addr;

#if 0
        // Get function name and offset from libunwind. Should match
        //  the libbacktrace code down below.
        unw_word_t offset;
        char function[512];
        function[0] = 0;
        unw_get_proc_name(&cursor, function, sizeof(function), &offset);
        printf ("0x%" PRIxPTR ": %s [+0x%" PRIxPTR "]\n", addr, function, offset);
#endif
    }

    return (int)count;
#endif
}

const char *
btrace_get_calling_module()
{
    unw_cursor_t cursor;
    unw_context_t context;
    const char *calling_fname = NULL;

    unw_getcontext(&context);
    unw_init_local(&cursor, &context);

    for(;;)
    {
        unw_word_t addr;
        Dl_info dl_info;

        if (unw_step(&cursor) <= 0)
            break;

        unw_get_reg(&cursor, UNW_REG_IP, &addr);

        // Get module name.
        if (dladdr((void *)addr, &dl_info) == 0)
            return NULL;

        if (dl_info.dli_fname)
        {
            if (!calling_fname)
            {
                calling_fname = dl_info.dli_fname;
            }
            else if(strcmp(calling_fname, dl_info.dli_fname))
            {
                return dl_info.dli_fname;
            }
        }
    }

    return NULL;
}

static int
btrace_module_search (const void *vkey, const void *ventry)
{
    const uintptr_t *key = (const uintptr_t *)vkey;
    const struct btrace_module_info *entry = (const struct btrace_module_info *) ventry;
    uintptr_t addr;

    addr = *key;
    if (addr < entry->base_address)
        return -1;
    else if (addr >= entry->base_address + entry->address_size)
        return 1;
    return 0;
}

const char *
btrace_get_current_module()
{
    void *paddr = __builtin_return_address(0);
    uintptr_t addr = (uintptr_t)paddr;
    std::lock_guard<std::mutex> lock(get_dlopen_mutex());

    // Try to search for the module name in our list. Should be faster than dladdr which
    //  goes through a bunch of symbol information.
    std::vector<btrace_module_info>& module_infos = get_module_infos();
    if (module_infos.size())
    {
        btrace_module_info *module_info = (btrace_module_info *)bsearch(&addr,
            &module_infos[0], module_infos.size(), sizeof(btrace_module_info), btrace_module_search);
        if (module_info && module_info->filename)
            return module_info->filename;
    }

    // Well, that failed for some reason. Try dladdr.
    Dl_info dl_info;
    if (dladdr(paddr, &dl_info) && dl_info.dli_fname)
        return dl_info.dli_fname;

    assert(false);
    return nullptr;
}

static void
btrace_err_callback(void */*data*/, const char *msg, int errnum)
{
    if (errnum == -1)
    {
        // Missing dwarf information. This happens when folks haven't compiled with -g or they
        //  stripped the symbols and we couldn't find em.
    }
    else
    {
        const char *errstr = errnum ? strerror(errnum) : "";
        printf("libbacktrace error: %s %s\n", msg, errstr);
        assert(false);
    }
}

static void
btrace_syminfo_callback(void *data, uintptr_t addr, const char *symname, uintptr_t symval, uintptr_t /*symsize*/)
{
    if (symname)
    {
        btrace_info *info = (btrace_info *)data;
        info->function = symname;
        info->offset = addr - symval;
    }
}

static int
btrace_pcinfo_callback(void *data, uintptr_t /*addr*/, const char *file, int line, const char *func)
{
    btrace_info *frame = (btrace_info *)data;

    frame->filename = file;
    frame->linenumber = line;

    // Don't overwrite function string if we got a blank one for some reason.
    if (func && func[0])
        frame->function = func;
    return 0;
}

static void
backtrace_initialize_error_callback(void */*data*/, const char */*msg*/, int /*errnum*/)
{
    // Backtrace_initialize only fails with alloc error and will be handled below.
}

static bool
module_info_init_state(btrace_module_info *module_info)
{
    if (!module_info->backtrace_state)
    {
        module_info->backtrace_state = backtrace_create_state(
                    module_info->filename, 0, backtrace_initialize_error_callback, NULL);
        if (module_info->backtrace_state)
        {
            elf_get_uuid(module_info->backtrace_state, module_info->filename,
                         module_info->uuid, &module_info->uuid_len);
        }
    }

    return !!module_info->backtrace_state;
}

bool
btrace_resolve_addr(btrace_info *info, uintptr_t addr, uint32_t flags)
{
    std::lock_guard<std::mutex> lock(get_dlopen_mutex());
    std::vector<btrace_module_info>& module_infos = get_module_infos();

    if (!module_infos.size())
        btrace_dlopen_notify_impl();

    info->addr = addr;
    info->offset = 0;
    info->module = NULL;
    info->function = NULL;
    info->filename = NULL;
    info->linenumber = 0;
    info->demangled_func_buf[0] = 0;

    btrace_module_info *module_info = (btrace_module_info *)bsearch(&addr,
        &module_infos[0], module_infos.size(), sizeof(btrace_module_info), btrace_module_search);
    if (module_info)
    {
        info->module = module_info->filename;

        if (module_info_init_state(module_info))
        {
            backtrace_fileline_initialize(module_info->backtrace_state, module_info->base_address,
                                          module_info->is_exe, backtrace_initialize_error_callback, NULL);

            // Get function name and offset.
            backtrace_syminfo(module_info->backtrace_state, addr, btrace_syminfo_callback,
                              btrace_err_callback, info);

            if (flags & BTRACE_RESOLVE_ADDR_GET_FILENAME)
            {
                // Get filename and line number (and maybe function).
                backtrace_pcinfo(module_info->backtrace_state, addr, btrace_pcinfo_callback,
                                 btrace_err_callback, info);
            }

            if ((flags & BTRACE_RESOLVE_ADDR_DEMANGLE_FUNC) && info->function && info->function[0])
            {
                info->function = btrace_demangle_function(info->function, info->demangled_func_buf, sizeof(info->demangled_func_buf));
            }
        }

        if (!info->offset)
            info->offset = addr - module_info->base_address;
    }

    // Get module name.
    if (!info->module || !info->module[0])
    {
        Dl_info dl_info;
        if (dladdr((void *)addr, &dl_info))
            info->module = dl_info.dli_fname;
        if (!info->offset)
            info->offset = addr - (uintptr_t)dl_info.dli_fbase;
    }

    if (info->module)
    {
        const char *module_name = strrchr(info->module, '/');
        if (module_name)
            info->module = module_name + 1;
    }

    if (!info->module)
        info->module = "";
    if (!info->function)
        info->function = "";
    if (!info->filename)
        info->filename = "";
    return 1;
}

static int
get_hex_value(char ch)
{
    if (ch >= 'A' && ch <= 'F')
        return 10 + ch - 'A';
    else if (ch >= 'a' && ch <= 'f')
        return 10 + ch - 'a';
    else if (ch >= '0' && ch <= '9')
        return ch - '0';

    return -1;
}

int
btrace_uuid_str_to_uuid(uint8_t uuid[20], const char *uuid_str)
{
    int len;

    for (len = 0; (len < 20) && *uuid_str; len++)
    {
        int val0 = get_hex_value(*uuid_str++);
        int val1 = get_hex_value(*uuid_str++);
        if (val0 < 0 || val1 < 0)
            break;
        uuid[len] = (val0 << 4) | val1;
    }

    return len;
}

void
btrace_uuid_to_str(char uuid_str[41], const uint8_t *uuid, int len)
{
    int i;
    static const char hex[] = "0123456789abcdef";

    if (len > 40)
        len = 40;
    for (i = 0; i < len; i++)
    {
        uint8_t c = *uuid++;

        *uuid_str++ = hex[c >> 4];
        *uuid_str++ = hex[c & 0xf];
    }
    *uuid_str = 0;
}

int
btrace_dump()
{
    int i;
    const int addrs_size = 128;
    uintptr_t addrs[addrs_size];
    int count = btrace_get(addrs, addrs_size, 0);

    for (i = 0; i < count; i++)
    {
        int ret;
        btrace_info info;

        ret = btrace_resolve_addr(&info, addrs[i],
                                BTRACE_RESOLVE_ADDR_GET_FILENAME | BTRACE_RESOLVE_ADDR_DEMANGLE_FUNC);

        printf(" 0x%" PRIxPTR " ", addrs[i]);
        if (ret)
        {
            printf("%s", info.module);

            if (info.function[0])
            {
                printf(": %s", info.function);
            }

            printf("+0x%" PRIxPTR, info.offset);

            if (info.filename && info.filename[0])
            {
                // Print last directory plus filename if possible.
                const char *slash_cur = info.filename;
                const char *slash_last = NULL;
                for (const char *ch = info.filename; *ch; ch++)
                {
                    if (*ch == '/')
                    {
                        slash_last = slash_cur;
                        slash_cur = ch + 1;
                    }
                }
                const char *filename = slash_last ? slash_last : slash_cur;
                printf(": %s:%d", filename, info.linenumber);
            }
        }

        printf("\n");
    }

    return count;
}

static int
dlopen_notify_callback(struct dl_phdr_info *info, size_t /*size*/, void *data)
{
    const int PATH_MAX = 1024;
    char buf[PATH_MAX];
    bool is_exe = false;
    const char *filename = info->dlpi_name;
    std::vector<btrace_module_info> *new_module_infos = (std::vector<btrace_module_info> *)data;
    std::vector<btrace_module_info>& module_infos = get_module_infos();

    // If we don't have a filename and we haven't added our main exe yet, do it.
    if (!filename || !filename[0])
    {
        if (!module_infos.size() && !new_module_infos->size())
        {
            is_exe = true;
            ssize_t ret =  readlink("/proc/self/exe", buf, sizeof(buf));
            if ((ret > 0) && (ret < (ssize_t)sizeof(buf)))
            {
                buf[ret] = 0;
                filename = buf;
            }
        }
        if (!filename || !filename[0])
            return 0;
    }

    uintptr_t addr_start = 0;
    uintptr_t addr_end = 0;

    for (int i = 0; i < info->dlpi_phnum; i++)
    {
        if (info->dlpi_phdr[i].p_type == PT_LOAD)
        {
            if (addr_end == 0)
            {
                addr_start = info->dlpi_addr + info->dlpi_phdr[i].p_vaddr;
                addr_end = addr_start + info->dlpi_phdr[i].p_memsz;
            }
            else
            {
                uintptr_t addr = info->dlpi_addr + info->dlpi_phdr[i].p_vaddr + info->dlpi_phdr[i].p_memsz;
                if (addr > addr_end)
                    addr_end = addr;
            }
        }
    }

    btrace_module_info module_info;
    module_info.base_address = addr_start;
    module_info.address_size = (uint32_t)(addr_end - addr_start);
    module_info.filename = filename;
    module_info.is_exe = is_exe;
    module_info.backtrace_state = NULL;
    module_info.uuid_len = 0;
    memset(module_info.uuid, 0, sizeof(module_info.uuid));

    auto it = std::lower_bound(module_infos.begin(), module_infos.end(), module_info);
    if (it == module_infos.end() || *it != module_info)
    {
        module_info.filename = strdup(filename);
        if (module_info.filename)
        {
            new_module_infos->push_back(module_info);
        }
    }
    return 0;
}

bool
btrace_dlopen_add_module(const btrace_module_info &module_info_in)
{
    std::lock_guard<std::mutex> lock(get_dlopen_mutex());
    std::vector<btrace_module_info>& module_infos = get_module_infos();

    auto it = std::lower_bound(module_infos.begin(), module_infos.end(), module_info_in);
    if (it == module_infos.end() || *it != module_info_in)
    {
        btrace_module_info module_info = module_info_in;

        if (module_info_init_state(&module_info))
        {
            // Make sure the UUID of the file on disk matches with what we were asked for.
            if ((module_info_in.uuid_len == module_info.uuid_len) &&
                    !memcmp(module_info_in.uuid, module_info.uuid, module_info.uuid_len))
            {
                module_infos.push_back(module_info);
                std::sort(module_infos.begin(), module_infos.end());
                return true;
            }
        }
    }

    return false;
}

const char *
btrace_get_debug_filename(const char *filename)
{
    std::lock_guard<std::mutex> lock(get_dlopen_mutex());
    std::vector<btrace_module_info>& module_infos = get_module_infos();

    std::string fname = filename;
    for (uint i = 0; i < module_infos.size(); i++)
    {
        btrace_module_info &module_info = module_infos[i];

        if (fname == module_info.filename)
        {
            if (module_info_init_state(&module_info))
            {
                backtrace_fileline_initialize(module_info.backtrace_state, module_info.base_address,
                                              module_info.is_exe, backtrace_initialize_error_callback, NULL);

                return backtrace_get_debug_filename(module_info.backtrace_state);
            }
        }
    }
    return NULL;
}

static void btrace_dlopen_notify_impl()
{
    std::vector<btrace_module_info> new_module_infos;

    // Iterator through all the currently loaded modules.
    dl_iterate_phdr(dlopen_notify_callback, &new_module_infos);

    if (new_module_infos.size())
    {
        std::vector<btrace_module_info>& module_infos = get_module_infos();
        module_infos.insert(module_infos.end(), new_module_infos.begin(), new_module_infos.end());
        std::sort(module_infos.begin(), module_infos.end());
    }
}

// This function is called from a dlopen hook, which means it could be
//  called from the driver or other code which hasn't aligned the stack.
#ifdef __i386
void __attribute__((force_align_arg_pointer))
#else
void
#endif
btrace_dlopen_notify(const char */*filename*/)
{
    std::lock_guard<std::mutex> lock(get_dlopen_mutex());
    btrace_dlopen_notify_impl();
}

//
// Other possibilities:
//   abi::__cxa_demangle() (cxxabi.h)
//   bfd_demangle (bfd.h)
//   cplus_demangle (demangle.h) libiberty code from gcc
//
#include <cxxabi.h>
// char * abi::__cxa_demangle(const char* __mangled_name, char* __output_buffer,
//    size_t* __length, int* __status);
// int status = 0;
// char *function = abi::__cxa_demangle(name, NULL, NULL, &status);
//
const char *
btrace_demangle_function(const char *name, char *buffer, size_t buflen)
{
    char *function = NULL;

    // Mangled-name is function or variable name...
    if (name[0] == '_' && name[1] == 'Z') {
        int status = 0;
        function = abi::__cxa_demangle(name, nullptr, nullptr, &status);
    }

    if (function && function[0])
        snprintf(buffer, buflen, "%s", function);
    else
        snprintf(buffer, buflen, "%s", name);

    buffer[buflen - 1] = 0;
    free(function);
    return buffer;
}
