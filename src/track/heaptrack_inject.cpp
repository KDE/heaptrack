/*
    SPDX-FileCopyrightText: 2014-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include "libheaptrack.h"
#include "util/config.h"
#include "util/linewriter.h"

#include <cstdlib>
#include <cstring>

#include <dlfcn.h>
#include <errno.h>
#include <link.h>
#include <unistd.h>

#include <sys/mman.h>
#include <limits.h>

#include <type_traits>

/**
 * @file heaptrack_inject.cpp
 *
 * @brief Experimental support for symbol overloading after runtime injection.
 */

#if ULONG_MAX == 0xffffffffffffffff
#define WORDSIZE 64
#elif ULONG_MAX == 0xffffffff
#define WORDSIZE 32
#endif

#ifndef ELF_R_SYM
#if WORDSIZE == 64
#define ELF_R_SYM(i) ELF64_R_SYM(i)
#elif WORDSIZE == 32
#define ELF_R_SYM(i) ELF32_R_SYM(i)
#else
#error unsupported word size
#endif
#endif

#ifndef ElfW
#if WORDSIZE == 64
#define ElfW(type) Elf64_##type
#elif WORDSIZE == 32
#define ElfW(type) Elf32_##type
#else
#error unsupported word size
#endif
#endif

// NOTE: adding noexcept to C functions is a hard error in clang++
//       (but not even a warning in GCC, even with -Wall)
#if defined(__GNUC__) && !defined(__clang__)
#define LIBC_FUN_ATTRS noexcept
#else
#define LIBC_FUN_ATTRS
#endif

extern "C" {

// Foward declare mimalloc (https://github.com/microsoft/mimalloc) functions so we don't need to include its .h.
__attribute__((weak)) void* mi_malloc(size_t size) LIBC_FUN_ATTRS;
__attribute__((weak)) void* mi_calloc(size_t count, size_t size) LIBC_FUN_ATTRS;
__attribute__((weak)) void* mi_realloc(void* p, size_t newsize) LIBC_FUN_ATTRS;
__attribute__((weak)) void  mi_free(void* p) LIBC_FUN_ATTRS;

}

namespace {

namespace Elf {
using Addr = ElfW(Addr);
using Dyn = ElfW(Dyn);
using Rel = ElfW(Rel);
using Rela = ElfW(Rela);
using Sym = ElfW(Sym);
#if WORDSIZE == 64
using Sxword = ElfW(Sxword);
using Xword = ElfW(Xword);
#else
// FreeBSD elf32.h doesn't define Elf32_Sxword or _Xword. This is used in struct
// elftable, where it's used as a tag value. Our Elf32_Dyn uses Elf32_Sword there,
// as does the Linux definition (and the standard); the El64_Dyn uses Sxword.
//
// Linux elf.h defines Elf32_Sxword as a 64-bit quantity, so let's do that
using Sxword = int64_t;
using Xword = uint64_t;
#endif
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
        auto inPtr = reinterpret_cast<uintptr_t>(ptr);
        auto ret = original(ptr, size);
        heaptrack_realloc2(inPtr, size, reinterpret_cast<uintptr_t>(ret));

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

// mimalloc functions
struct mi_malloc
{
    static constexpr auto name = "mi_malloc";
    static constexpr auto original = &::mi_malloc;

    static void* hook(size_t size) noexcept
    {
        auto ptr = original(size);
        heaptrack_malloc(ptr, size);
        return ptr;
    }
};

struct mi_free
{
    static constexpr auto name = "mi_free";
    static constexpr auto original = &::mi_free;

    static void hook(void* ptr) noexcept
    {
        heaptrack_free(ptr);
        original(ptr);
    }
};

struct mi_realloc
{
    static constexpr auto name = "mi_realloc";
    static constexpr auto original = &::mi_realloc;

    static void* hook(void* ptr, size_t size) noexcept
    {
        auto ret = original(ptr, size);
        heaptrack_realloc(ptr, size, ret);
        return ret;
    }
};

struct mi_calloc
{
    static constexpr auto name = "mi_calloc";
    static constexpr auto original = &::mi_calloc;

    static void* hook(size_t num, size_t size) noexcept
    {
        auto ptr = original(num, size);
        heaptrack_malloc(ptr, num * size);
        return ptr;
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
        || hook<dlclose>(symname, addr, restore)
        // mimalloc functions
        || hook<mi_malloc>(symname, addr, restore) || hook<mi_free>(symname, addr, restore) || hook<mi_realloc>(symname, addr, restore)
        || hook<mi_calloc>(symname, addr, restore);
}
}

template <typename T, Elf::Sxword AddrTag, Elf::Sxword SizeTag>
struct elftable
{
    using type = T;
    Elf::Addr table = 0;
    Elf::Xword size = 0;

    bool consume(const Elf::Dyn* dyn) noexcept
    {
        if (dyn->d_tag == AddrTag) {
            table = dyn->d_un.d_ptr;
            return true;
        } else if (dyn->d_tag == SizeTag) {
            size = dyn->d_un.d_val;
            return true;
        }
        return false;
    }

    explicit operator bool() const noexcept
    {
        return table && size;
    }

    T* start(Elf::Addr tableOffset) const noexcept
    {
        return reinterpret_cast<T*>(table + tableOffset);
    }

    T* end(Elf::Addr tableOffset) const noexcept
    {
        return reinterpret_cast<T*>(table + tableOffset + size);
    }
};

using elf_string_table = elftable<const char, DT_STRTAB, DT_STRSZ>;
using elf_rel_table = elftable<Elf::Rel, DT_REL, DT_RELSZ>;
using elf_rela_table = elftable<Elf::Rela, DT_RELA, DT_RELASZ>;
using elf_jmprel_table = elftable<Elf::Rela, DT_JMPREL, DT_PLTRELSZ>;
using elf_symbol_table = elftable<const Elf::Sym, DT_SYMTAB, DT_SYMENT>;

template <typename Table>
void try_overwrite_elftable(const Table& jumps, const elf_string_table& strings, const elf_symbol_table& symbols,
                            const Elf::Addr base, const bool restore) noexcept
{
    Elf::Addr tableOffset =
#ifdef __linux__
        0; // Already has memory addresses
#elif defined(__FreeBSD__)
        base; // Only has ELF offsets
#else
#error port me
#endif

    const auto rela_start = jumps.start(tableOffset);
    const auto rela_end = jumps.end(tableOffset);

    const auto sym_start = symbols.start(tableOffset);
    // we have no total size of the symbol table, so we cannot test that for validity

    const auto str_start = strings.start(tableOffset);
    const auto str_end = strings.end(tableOffset);
    const auto num_str = static_cast<uintptr_t>(str_end - str_start);

    for (auto rela = rela_start; rela < rela_end; rela++) {
        const auto sym_index = ELF_R_SYM(rela->r_info);
        const auto str_index = sym_start[sym_index].st_name;
        if (str_index < 0 || str_index >= num_str)
            continue;

        const char* symname = str_start + str_index;

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

    if (!symbols || !strings) {
        return;
    }

    // find symbols to overwrite
    if (rels) {
        try_overwrite_elftable(rels, strings, symbols, base, restore);
    }

    if (relas) {
        try_overwrite_elftable(relas, strings, symbols, base, restore);
    }

    if (jmprels) {
        try_overwrite_elftable(jmprels, strings, symbols, base, restore);
    }
}

int iterate_phdrs(dl_phdr_info* info, size_t /*size*/, void* data) noexcept
{
    if (strstr(info->dlpi_name, "/libheaptrack_inject.so")) {
        // prevent infinite recursion: do not overwrite our own symbols
        return 0;
    } else if (strstr(info->dlpi_name, "/ld-linux")) {
        // prevent strange crashes due to overwriting the free symbol in ld-linux
        // (doesn't seem to be necessary in FreeBSD's ld-elf)
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

void restore_symbols() noexcept
{
    bool do_shutdown = true;
    dl_iterate_phdr(&iterate_phdrs, &do_shutdown);
}
}

extern "C" {
// this function is called when heaptrack_inject is runtime injected via GDB
void heaptrack_inject(const char* outputFileName) noexcept
{
    heaptrack_init(
        outputFileName, &overwrite_symbols, [](LineWriter& out) { out.write("A\n"); }, &restore_symbols);
}
}

// alternatively, the code below may initialize heaptrack when we use
// heaptrack_inject via LD_PRELOAD and have the right environment variables setup
struct HeaptrackInjectPreloadInitialization
{
    HeaptrackInjectPreloadInitialization()
    {
        const auto outputFileName = getenv("DUMP_HEAPTRACK_OUTPUT");
        if (!outputFileName) {
            // when the env var wasn't set, then this means we got runtime injected, don't do anything here
            return;
        }
        heaptrack_init(outputFileName, &overwrite_symbols, nullptr, &restore_symbols);
    }
};

static HeaptrackInjectPreloadInitialization heaptrackInjectPreloadInitialization;
