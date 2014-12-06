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
#include <string.h>
#include <stdlib.h>

/**
 * @file heaptrack_inject.cpp
 *
 * @brief Experimental support for symbol overloading after runtime injection.
 */

namespace {

namespace hooks {

struct malloc
{
    static constexpr auto name = "malloc";
    static constexpr auto original = &::malloc;
    using Signature = void* (*) (size_t);

    static void* hook(size_t size)
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
    using Signature = void (*) (void*);

    static void hook(void *ptr)
    {
        heaptrack_free(ptr);
        original(ptr);
    }
};

}


template<typename T, ElfW(Sxword) AddrTag, ElfW(Sxword) SizeTag>
struct elftable
{
    T* table = nullptr;
    ElfW(Xword) size = ElfW(Xword)();

    bool consume(const ElfW(Dyn) *dyn)
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

void try_overwrite_symbols(const ElfW(Dyn) *dyn, const ElfW(Addr) base)
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
        auto addr = rela->r_offset + base;
        if (!strcmp(hooks::malloc::name, relsymname)) {
            *reinterpret_cast<hooks::malloc::Signature*>(addr) = &hooks::malloc::hook;
        } else if (!strcmp(hooks::free::name, relsymname)) {
            *reinterpret_cast<hooks::free::Signature*>(addr) = &hooks::free::hook;
        }
    }
}

int iterate_phdrs(dl_phdr_info *info, size_t /*size*/, void *data)
{
    if (strstr(info->dlpi_name, "/ld-linux-x86-64.so") || strstr(info->dlpi_name, "/libheaptrackinject.so")) {
        /// FIXME: why are these checks required? If I don't filter them out, we'll crash
        /// when trying to write the malloc symbols found
        return 0;
    }

    for (auto phdr = info->dlpi_phdr, end = phdr + info->dlpi_phnum; phdr != end; ++phdr) {
        if (phdr->p_type == PT_DYNAMIC && (phdr->p_flags & (PF_W | PF_R)) == (PF_W | PF_R)) {
            try_overwrite_symbols(reinterpret_cast<const ElfW(Dyn) *>(phdr->p_vaddr + info->dlpi_addr),
                                  info->dlpi_addr);
        }
    }
    return 0;
}

struct InitializeInjection
{
    InitializeInjection()
    {
        dl_iterate_phdr(&iterate_phdrs, nullptr);
        heaptrack_init();
    }
} initialize;

}
