/*
 * Copyright 2014 Milian Wolff <mail@milianw.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "libheaptrack.h"

#include <link.h>
#include <cstring>
#include <cstdlib>
#include <sys/mman.h>
#include <malloc.h>

#include <type_traits>

/**
 * @file heaptrack_inject.cpp
 *
 * @brief Experimental support for symbol overloading after runtime injection.
 */

namespace {

void overwrite_symbols() noexcept;

namespace hooks {

struct malloc
{
    static constexpr auto name = "malloc";
    static constexpr auto original = &::malloc;

    static void* hook(size_t size) noexcept
    {
        auto ptr = original(size);
        heaptrack_malloc(ptr, size);
        return ptr;
    }
};

struct free
{
    static constexpr auto name = "free";
    static constexpr auto original = &::free;

    static void hook(void *ptr) noexcept
    {
        heaptrack_free(ptr);
        original(ptr);
    }
};

struct realloc
{
    static constexpr auto name = "realloc";
    static constexpr auto original = &::realloc;

    static void* hook(void *ptr, size_t size) noexcept
    {
        auto ret = original(ptr, size);
        heaptrack_realloc(ptr, size, ret);
        return ret;
    }
};

struct calloc
{
    static constexpr auto name = "calloc";
    static constexpr auto original = &::calloc;

    static void* hook(size_t num, size_t size) noexcept
    {
        auto ptr = original(num, size);
        heaptrack_malloc(ptr, num * size);
        return ptr;
    }
};

struct cfree
{
    static constexpr auto name = "cfree";
    static constexpr auto original = &::cfree;

    static void hook(void *ptr) noexcept
    {
        heaptrack_free(ptr);
        original(ptr);
    }
};

struct dlopen
{
    static constexpr auto name = "dlopen";
    static constexpr auto original = &::dlopen;

    static void* hook(const char *filename, int flag) noexcept
    {
        auto ret = original(filename, flag);
        if (ret) {
            heaptrack_invalidate_module_cache();
            overwrite_symbols();
        }
        return ret;
    }
};

struct dlclose
{
    static constexpr auto name = "dlclose";
    static constexpr auto original = &::dlclose;

    static int hook(void *handle) noexcept
    {
        auto ret = original(handle);
        if (!ret) {
            heaptrack_invalidate_module_cache();
        }
        return ret;
    }
};

struct posix_memalign
{
    static constexpr auto name = "posix_memalign";
    static constexpr auto original = &::posix_memalign;

    static int hook(void **memptr, size_t alignment, size_t size) noexcept
    {
        auto ret = original(memptr, alignment, size);
        if (!ret) {
            heaptrack_malloc(*memptr, size);
        }
        return ret;
    }
};

struct hook
{
    const char * const name;
    void* address;
    void* originalAddress;

    template<typename Hook>
    static constexpr hook wrap() noexcept
    {
        static_assert(sizeof(&Hook::hook) == sizeof(void*), "Mismatched pointer sizes");
        static_assert(std::is_convertible<decltype(&Hook::hook), decltype(Hook::original)>::value,
                      "hook is not compatible to original function");
        // TODO: why is (void*) cast allowed, but not reinterpret_cast?
        return {Hook::name, (void*)(&Hook::hook), (void*)(Hook::original)};
    }
};

constexpr hook list[] = {
    hook::wrap<malloc>(),
    hook::wrap<free>(),
    hook::wrap<realloc>(),
    hook::wrap<calloc>(),
    hook::wrap<cfree>(),
    hook::wrap<posix_memalign>(),
    hook::wrap<dlopen>(),
    hook::wrap<dlclose>(),
};

}

template<typename T, ElfW(Sxword) AddrTag, ElfW(Sxword) SizeTag>
struct elftable
{
    T* table = nullptr;
    ElfW(Xword) size = ElfW(Xword)();

    bool consume(const ElfW(Dyn) *dyn) noexcept
    {
        if (dyn->d_tag == AddrTag) {
            table = reinterpret_cast<T*>(dyn->d_un.d_ptr);
            return true;
        } else if (dyn->d_tag == SizeTag) {
            size = dyn->d_un.d_val;
            return true;
        }
        return false;
    }
};

using elf_string_table = elftable<const char, DT_STRTAB, DT_STRSZ>;
using elf_jmprel_table = elftable<ElfW(Rela), DT_JMPREL, DT_PLTRELSZ>;
using elf_symbol_table = elftable<ElfW(Sym), DT_SYMTAB, DT_SYMENT>;

void try_overwrite_symbols(const ElfW(Dyn) *dyn, const ElfW(Addr) base, const bool restore) noexcept
{
    elf_symbol_table symbols;
    elf_jmprel_table jmprels;
    elf_string_table strings;

    for (; dyn->d_tag != DT_NULL; ++dyn) {
        symbols.consume(dyn) || jmprels.consume(dyn) || strings.consume(dyn);
    }

    auto relaend = reinterpret_cast<ElfW(Rela) *>(reinterpret_cast<char *>(jmprels.table) + jmprels.size);
    for (auto rela = jmprels.table; rela < relaend; rela++) {
        auto relsymidx = ELF64_R_SYM(rela->r_info);
        const char *relsymname = strings.table + symbols.table[relsymidx].st_name;
        auto addr = reinterpret_cast<void**>(rela->r_offset + base);
        for (const auto& hook : hooks::list) {
            if (!strcmp(hook.name, relsymname)) {
                // try to make the page read/write accessible, which is hackish
                // but apparently required for some shared libraries
                void *page = (void *)((intptr_t)addr & ~(0x1000 - 1));
                mprotect(page, 0x1000, PROT_READ | PROT_WRITE);
                if (restore) {
                    // restore the original address on shutdown
                    *addr = hook.originalAddress;
                } else {
                    // now actually inject our hook
                    *addr = hook.address;
                }
                break;
            }
        }
    }
}

int iterate_phdrs(dl_phdr_info *info, size_t /*size*/, void *data) noexcept
{
    if (strstr(info->dlpi_name, "/libheaptrack_inject.so")) {
        // prevent infinite recursion: do not overwrite our own symbols
        return 0;
    }

    for (auto phdr = info->dlpi_phdr, end = phdr + info->dlpi_phnum; phdr != end; ++phdr) {
        if (phdr->p_type == PT_DYNAMIC) {
            try_overwrite_symbols(reinterpret_cast<const ElfW(Dyn) *>(phdr->p_vaddr + info->dlpi_addr),
                                  info->dlpi_addr, data != nullptr);
        }
    }
    return 0;
}

void overwrite_symbols() noexcept
{
    dl_iterate_phdr(&iterate_phdrs, nullptr);
}

}

extern "C" {

void heaptrack_inject(const char *outputFileName) noexcept
{
    heaptrack_init(outputFileName, [] () {
        overwrite_symbols();
    }, [] (FILE* out) {
        fprintf(out, "A BEGIN_MALLOC_INFO\n");
        malloc_info(0, out);
        fprintf(out, "\nA END_MALLOC_INFO\n");
    }, [] () {
        bool do_shutdown = true;
        dl_iterate_phdr(&iterate_phdrs, &do_shutdown);
    });
}

}
