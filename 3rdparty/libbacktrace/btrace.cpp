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
#include <cxxabi.h>

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

namespace {

struct btrace_module_info
{
    //$ TODO mikesart: need an ID number in here. This will get incremented
    // everytime we have an address conflict. All stack traces should get this
    // ID so they know which module they should get symbols from.
    uintptr_t base_address;
    uint32_t address_size;
    struct backtrace_state *backtrace_state;
    const char *filename;
    int is_exe;
};

inline bool operator<(const btrace_module_info &info1, const btrace_module_info &info2)
{
    if (info1.base_address < info2.base_address) {
        return true;
    }
    if (info1.base_address == info2.base_address) {
        if (info1.address_size < info2.address_size) {
            return true;
        }
        if (info1.address_size == info2.address_size) {
            if (strcmp(info1.filename, info2.filename) < 0) {
                return false;
            }
        }
    }
    return false;
}

inline bool operator==(const btrace_module_info &info1, const btrace_module_info &info2)
{
    return info1.base_address == info2.base_address &&
           info1.address_size == info2.address_size &&
           !strcmp(info1.filename, info2.filename);
}

inline bool operator!=(const btrace_module_info &info1, const btrace_module_info &info2)
{
    return !(info1 == info2);
}

// Our dlopen mutex to protect g_module_infos operations
std::mutex &get_dlopen_mutex()
{
    static std::mutex g_dlopen_mutex;
    return g_dlopen_mutex;
}

// An array of all the modules that have been loaded in this process.
std::vector<btrace_module_info> &get_module_infos()
{
    static std::vector<btrace_module_info> s_module_infos;
    return s_module_infos;
}

void btrace_err_callback(void */*data*/, const char *msg, int errnum)
{
    if (errnum == -1) {
        // Missing dwarf information. This happens when folks haven't compiled with -g or they
        //  stripped the symbols and we couldn't find em.
    } else {
        const char *errstr = errnum ? strerror(errnum) : "";
        printf("libbacktrace error: %s %s\n", msg, errstr);
        assert(false);
    }
}

void btrace_syminfo_callback(void *data, uintptr_t addr, const char *symname, uintptr_t symval, uintptr_t /*symsize*/)
{
    if (symname) {
        btrace_info *info = (btrace_info *)data;
        info->function = symname;
        info->offset = addr - symval;
    }
}

int btrace_pcinfo_callback(void *data, uintptr_t /*addr*/, const char *file, int line, const char *func)
{
    btrace_info *frame = (btrace_info *)data;

    frame->filename = file;
    frame->linenumber = line;

    // Don't overwrite function string if we got a blank one for some reason.
    if (func && func[0]) {
        frame->function = func;
    }
    return 0;
}

void backtrace_initialize_error_callback(void */*data*/, const char */*msg*/, int /*errnum*/)
{
    // Backtrace_initialize only fails with alloc error and will be handled below.
}

bool module_info_init_state(btrace_module_info *module_info)
{
    if (!module_info->backtrace_state) {
        module_info->backtrace_state = backtrace_create_state(module_info->filename, 0,
                                                              backtrace_initialize_error_callback, nullptr);
    }

    return module_info->backtrace_state;
}

int dlopen_notify_callback(struct dl_phdr_info *info, size_t /*size*/, void */*data*/)
{
    bool is_exe = false;
    const char *filename = info->dlpi_name;
    std::vector<btrace_module_info>& module_infos = get_module_infos();

    const int BUF_SIZE = 1024;
    char buf[BUF_SIZE];
    // If we don't have a filename and we haven't added our main exe yet, do it.
    if (!filename || !filename[0]) {
        if (!module_infos.size()) {
            is_exe = true;
            ssize_t ret =  readlink("/proc/self/exe", buf, sizeof(buf));
            if ((ret > 0) && (ret < (ssize_t)sizeof(buf))) {
                buf[ret] = 0;
                filename = buf;
            }
        }
        if (!filename || !filename[0]) {
            return 0;
        }
    }

    uintptr_t addr_start = 0;
    uintptr_t addr_end = 0;

    for (int i = 0; i < info->dlpi_phnum; i++) {
        if (info->dlpi_phdr[i].p_type == PT_LOAD) {
            if (addr_end == 0) {
                addr_start = info->dlpi_addr + info->dlpi_phdr[i].p_vaddr;
                addr_end = addr_start + info->dlpi_phdr[i].p_memsz;
            } else {
                uintptr_t addr = info->dlpi_addr + info->dlpi_phdr[i].p_vaddr + info->dlpi_phdr[i].p_memsz;
                if (addr > addr_end) {
                    addr_end = addr;
                }
            }
        }
    }

    btrace_module_info module_info;
    module_info.base_address = addr_start;
    module_info.address_size = (uint32_t)(addr_end - addr_start);
    module_info.filename = filename;
    module_info.is_exe = is_exe;
    module_info.backtrace_state = nullptr;

    auto it = std::lower_bound(module_infos.begin(), module_infos.end(), module_info);
    if (it == module_infos.end() || *it != module_info) {
        module_info.filename = strdup(filename);
        if (module_info.filename) {
            module_infos.insert(it, module_info);
        }
    }
    return 0;
}

// like btrace_dlopen_notify, but must be called while already holding the mutex lock
void btrace_dlopen_notify_impl()
{
    // iterate through all currently loaded modules.
    dl_iterate_phdr(dlopen_notify_callback, nullptr);
}

}

bool btrace_resolve_addr(btrace_info *info, uintptr_t addr, ResolveFlags flags)
{
    std::lock_guard<std::mutex> lock(get_dlopen_mutex());
    std::vector<btrace_module_info>& module_infos = get_module_infos();

    if (!module_infos.size()) {
        btrace_dlopen_notify_impl();
    }

    info->addr = addr;
    info->offset = 0;
    info->module = nullptr;
    info->function = nullptr;
    info->filename = nullptr;
    info->linenumber = 0;
    info->demangled_func_buf[0] = 0;

    auto module_info = std::lower_bound(module_infos.begin(), module_infos.end(), addr,
                                        [] (const btrace_module_info& info, const uintptr_t addr) -> bool {
                                            return info.base_address + info.address_size < addr;
                                        });

    if (module_info != module_infos.end()) {
        info->module = module_info->filename;

        if (module_info_init_state(&*module_info)) {
            backtrace_fileline_initialize(module_info->backtrace_state, module_info->base_address,
                                          module_info->is_exe, backtrace_initialize_error_callback, nullptr);

            // Get function name and offset.
            backtrace_syminfo(module_info->backtrace_state, addr, btrace_syminfo_callback,
                              btrace_err_callback, info);

            if (flags & GET_FILENAME) {
                // Get filename and line number (and maybe function).
                backtrace_pcinfo(module_info->backtrace_state, addr, btrace_pcinfo_callback,
                                 btrace_err_callback, info);
            }

            if ((flags & DEMANGLE_FUNC) && info->function && info->function[0]) {
                info->function = btrace_demangle_function(info->function, info->demangled_func_buf, sizeof(info->demangled_func_buf));
            }
        }

        if (!info->offset) {
            info->offset = addr - module_info->base_address;
        }
    }

    // Get module name.
    if (!info->module || !info->module[0]) {
        Dl_info dl_info;
        if (dladdr((void *)addr, &dl_info)) {
            info->module = dl_info.dli_fname;
        }
        if (!info->offset) {
            info->offset = addr - (uintptr_t)dl_info.dli_fbase;
        }
    }

    if (info->module) {
        const char *module_name = strrchr(info->module, '/');
        if (module_name) {
            info->module = module_name + 1;
        }
    }

    if (!info->module) {
        info->module = "";
    }
    if (!info->function) {
        info->function = "";
    }
    if (!info->filename) {
        info->filename = "";
    }
    return 1;
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

const char * btrace_demangle_function(const char *name, char *buffer, size_t buflen)
{
    char *function = nullptr;

    // Mangled-name is function or variable name...
    if (name[0] == '_' && name[1] == 'Z') {
        int status = 0;
        function = abi::__cxa_demangle(name, nullptr, nullptr, &status);
    }

    if (function && function[0]) {
        snprintf(buffer, buflen, "%s", function);
    } else {
        snprintf(buffer, buflen, "%s", name);
    }

    buffer[buflen - 1] = 0;
    free(function);
    return buffer;
}
