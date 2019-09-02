/*
 * Copyright 2014-2017 Milian Wolff <mail@milianw.de>
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

#include "libheaptrack.h"
#include "util/config.h"
#include "util/linewriter.h"

#include <cstdlib>
#include <cstring>

#include <dlfcn.h>
#include <errno.h>
#include <link.h>
#include <malloc.h>
#include <unistd.h>

#include <sys/mman.h>

#include <type_traits>

/**
 * @file heaptrack_inject.cpp
 *
 * @brief Experimental support for symbol overloading after runtime injection.
 */

#if __WORDSIZE == 64
#define ELF_R_SYM(i) ELF64_R_SYM(i)
#elif __WORDSIZE == 32
#define ELF_R_SYM(i) ELF32_R_SYM(i)
#else
#error unsupported word size
#endif

namespace {

namespace Elf {
using Addr = ElfW(Addr);
using Dyn = ElfW(Dyn);
using Rel = ElfW(Rel);
using Rela = ElfW(Rela);
using Sym = ElfW(Sym);
using Sxword = ElfW(Sxword);
using Xword = ElfW(Xword);
}

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

    static void hook(void* ptr) noexcept
    {
        heaptrack_free(ptr);
        original(ptr);
    }
};

struct realloc
{
    static constexpr auto name = "realloc";
    static constexpr auto original = &::realloc;

    static void* hook(void* ptr, size_t size) noexcept
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

#if HAVE_CFREE
struct cfree
{
    static constexpr auto name = "cfree";
    static constexpr auto original = &::cfree;

    static void hook(void* ptr) noexcept
    {
        heaptrack_free(ptr);
        original(ptr);
    }
};
#endif

struct dlopen
{
    static constexpr auto name = "dlopen";
    static constexpr auto original = &::dlopen;

    static void* hook(const char* filename, int flag) noexcept
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

    static int hook(void* handle) noexcept
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

    static int hook(void** memptr, size_t alignment, size_t size) noexcept
    {
        auto ret = original(memptr, alignment, size);
        if (!ret) {
            heaptrack_malloc(*memptr, size);
        }
        return ret;
    }
};

template <typename Hook>
bool hook(const char* symname, Elf::Addr addr, bool restore)
{
    static_assert(std::is_convertible<decltype(&Hook::hook), decltype(Hook::original)>::value,
                  "hook is not compatible to original function");

    if (strcmp(Hook::name, symname) != 0) {
        return false;
    }

    // try to make the page read/write accessible, which is hackish
    // but apparently required for some shared libraries
    auto page = reinterpret_cast<void*>(addr & ~(0x1000 - 1));
    mprotect(page, 0x1000, PROT_READ | PROT_WRITE);

    // now write to the address
    auto typedAddr = reinterpret_cast<typename std::remove_const<decltype(Hook::original)>::type*>(addr);
    if (restore) {
        // restore the original address on shutdown
        *typedAddr = Hook::original;
    } else {
        // now actually inject our hook
        *typedAddr = &Hook::hook;
    }

    return true;
}

void apply(const char* symname, Elf::Addr addr, bool restore)
{
    // TODO: use std::apply once we can rely on C++17
    hook<malloc>(symname, addr, restore) || hook<free>(symname, addr, restore) || hook<realloc>(symname, addr, restore)
        || hook<calloc>(symname, addr, restore)
#if HAVE_CFREE
        || hook<cfree>(symname, addr, restore)
#endif
        || hook<posix_memalign>(symname, addr, restore) || hook<dlopen>(symname, addr, restore)
        || hook<dlclose>(symname, addr, restore);
}
}

template <typename T, Elf::Sxword AddrTag, Elf::Sxword SizeTag>
struct elftable
{
    using type = T;
    T* table = nullptr;
    Elf::Xword size = {};

    bool consume(const Elf::Dyn* dyn) noexcept
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
using elf_rel_table = elftable<Elf::Rel, DT_REL, DT_RELSZ>;
using elf_rela_table = elftable<Elf::Rela, DT_RELA, DT_RELASZ>;
using elf_jmprel_table = elftable<Elf::Rela, DT_JMPREL, DT_PLTRELSZ>;
using elf_symbol_table = elftable<Elf::Sym, DT_SYMTAB, DT_SYMENT>;

template <typename Table>
void try_overwrite_elftable(const Table& jumps, const elf_string_table& strings, const elf_symbol_table& symbols,
                            const Elf::Addr base, const bool restore) noexcept
{
    const auto rela_end = reinterpret_cast<typename Table::type*>(reinterpret_cast<char*>(jumps.table) + jumps.size);
    for (auto rela = jumps.table; rela < rela_end; rela++) {
        const auto index = ELF_R_SYM(rela->r_info);
        const char* symname = strings.table + symbols.table[index].st_name;
        auto addr = rela->r_offset + base;
        hooks::apply(symname, addr, restore);
    }
}

void try_overwrite_symbols(const Elf::Dyn* dyn, const Elf::Addr base, const bool restore) noexcept
{
    elf_symbol_table symbols;
    elf_rel_table rels;
    elf_rela_table relas;
    elf_jmprel_table jmprels;
    elf_string_table strings;

    // initialize the elf tables
    for (; dyn->d_tag != DT_NULL; ++dyn) {
        symbols.consume(dyn) || strings.consume(dyn) || rels.consume(dyn) || relas.consume(dyn) || jmprels.consume(dyn);
    }

    // find symbols to overwrite
    try_overwrite_elftable(rels, strings, symbols, base, restore);
    try_overwrite_elftable(relas, strings, symbols, base, restore);
    try_overwrite_elftable(jmprels, strings, symbols, base, restore);
}

int iterate_phdrs(dl_phdr_info* info, size_t /*size*/, void* data) noexcept
{
    if (strstr(info->dlpi_name, "/libheaptrack_inject.so")) {
        // prevent infinite recursion: do not overwrite our own symbols
        return 0;
    } else if (strstr(info->dlpi_name, "/ld-linux")) {
        // prevent strange crashes due to overwriting the free symbol in ld-linux
        return 0;
    }

    for (auto phdr = info->dlpi_phdr, end = phdr + info->dlpi_phnum; phdr != end; ++phdr) {
        if (phdr->p_type == PT_DYNAMIC) {
            try_overwrite_symbols(reinterpret_cast<const Elf::Dyn*>(phdr->p_vaddr + info->dlpi_addr), info->dlpi_addr,
                                  data != nullptr);
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
void heaptrack_inject(const char* outputFileName) noexcept
{
    heaptrack_init(outputFileName, []() { overwrite_symbols(); }, [](LineWriter& out) { out.write("A\n"); },
                   []() {
                       bool do_shutdown = true;
                       dl_iterate_phdr(&iterate_phdrs, &do_shutdown);
                   });
}
}
