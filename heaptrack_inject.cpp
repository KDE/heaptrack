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

#include <stdio.h>
#include <link.h>
#include <string.h>
#include <stdlib.h>

/**
 * @file heaptrack_inject.cpp
 *
 * @brief Experimental support for symbol overloading after runtime injection.
 */

namespace {

auto original_malloc = &malloc;

void* overwrite_malloc(size_t size)
{
    auto ret = original_malloc(size);
    printf("HALLOOOOOO! %lu %p\n", size, ret);
    return ret;
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

bool overwrite_symbols(const ElfW(Dyn) *dyn)
{
    elf_symbol_table symbols;
    elf_jmprel_table jmprels;
    elf_string_table strings;
    for (; dyn->d_tag != DT_NULL; ++dyn) {
        symbols.consume(dyn) || jmprels.consume(dyn) || strings.consume(dyn);
    }
    printf("\tfound:\tsymtab: %p = %lu, jmptab: %p = %lu, strtab %p = %lu\n",
            symbols.table, symbols.size,
            jmprels.table, jmprels.size,
            strings.table, strings.size);

    auto relaend = reinterpret_cast<ElfW(Rela) *>(reinterpret_cast<char *>(jmprels.table) + jmprels.size);
    for (auto rela = jmprels.table; rela < relaend; rela++) {
        auto relsymidx = ELF64_R_SYM(rela->r_info);
        const char *relsymname = strings.table + symbols.table[relsymidx].st_name;
        if (strcmp("malloc", relsymname) == 0) {
            printf("!!!!!!!!!1 found malloc: %p\n", reinterpret_cast<void *>(rela->r_offset));
            *reinterpret_cast<void*(**)(size_t)>(rela->r_offset) = &overwrite_malloc;
            return true;
        }
    }
    return false;
}

int iterate_phdrs(dl_phdr_info *info, size_t /*size*/, void *data)
{
    printf("  iterate dlpi: %s\n", info->dlpi_name);
    for (auto phdr = info->dlpi_phdr, end = phdr + info->dlpi_phnum; phdr != end; ++phdr) {
        if (phdr->p_type == PT_DYNAMIC && (phdr->p_flags & (PF_W | PF_R)) == (PF_W | PF_R)) {
            printf("    try dyn with flags=%d\n", phdr->p_flags);
            if (overwrite_symbols(reinterpret_cast<const ElfW(Dyn) *>(phdr->p_vaddr + info->dlpi_addr))) {
//                 return 1;
// NOTE: stopping at the first found malloc symbol makes the "simple" demo work,
// but more complicated apps with shared libraries still don't work as expected
            }
        }
    }
    return 0;
}

struct InitializeInjection
{
    InitializeInjection()
    {
        printf("init\n");
        dl_iterate_phdr(&iterate_phdrs, nullptr);
    }
} initialize;

}
