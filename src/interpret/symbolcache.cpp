/*
    symbolcache.cpp

    SPDX-FileCopyrightText: 2022 Klarälvdalens Datakonsult AB a KDAB Group company <info@kdab.com>
    SPDX-FileContributor: Milian Wolff <milian.wolff@kdab.com>

   SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "symbolcache.h"

#include "dwarfdiecache.h"

#include <algorithm>

static bool operator<(const SymbolCache::SymbolCacheEntry& lhs, const SymbolCache::SymbolCacheEntry& rhs)
{
    return lhs.offset < rhs.offset;
}

static bool operator==(const SymbolCache::SymbolCacheEntry& lhs, const SymbolCache::SymbolCacheEntry& rhs)
{
    return lhs.offset == rhs.offset && lhs.size == rhs.size;
}

static bool operator<(const SymbolCache::SymbolCacheEntry& lhs, uint64_t addr)
{
    return lhs.offset < addr;
}

bool SymbolCache::hasSymbols(const std::string& filePath) const
{
    return m_symbolCache.contains(filePath);
}

SymbolCache::SymbolCacheEntry SymbolCache::findSymbol(const std::string& filePath, uint64_t relAddr)
{
    auto& symbols = m_symbolCache[filePath];
    auto it = std::lower_bound(symbols.begin(), symbols.end(), relAddr);

    // demangle symbols on demand instead of demangling all symbols directly
    // hopefully most of the symbols we won't ever encounter after all
    auto lazyDemangle = [](SymbolCache::SymbolCacheEntry& entry) {
        if (!entry.demangled) {
            entry.symname = demangle(entry.symname);
            entry.demangled = true;
        }
        return entry;
    };

    if (it != symbols.end() && it->offset == relAddr)
        return lazyDemangle(*it);
    if (it == symbols.begin())
        return {};

    --it;

    if (it->offset <= relAddr && (it->offset + it->size > relAddr || (it->size == 0))) {
        return lazyDemangle(*it);
    }
    return {};
}

void SymbolCache::setSymbols(const std::string& filePath, Symbols symbols)
{
    /*
     * use stable_sort to produce results that are comparable to what addr2line would
     * return when we have entries like this in the symtab:
     *
     * 000000000045a130 l     F .text  0000000000000033 .hidden __memmove_avx_unaligned
     * 000000000045a180 l     F .text  00000000000003d8 .hidden __memmove_avx_unaligned_erms
     * 000000000045a180 l     F .text  00000000000003d8 .hidden __memcpy_avx_unaligned_erms
     * 000000000045a130 l     F .text  0000000000000033 .hidden __memcpy_avx_unaligned
     *
     * here, addr2line would always find the first entry. we want to do the same
     */

    std::stable_sort(symbols.begin(), symbols.end());
    symbols.erase(std::unique(symbols.begin(), symbols.end()), symbols.end());
    m_symbolCache[filePath] = std::move(symbols);
}
