/*
    dwarfdiecache.cpp

    SPDX-FileCopyrightText: 2022 Klarälvdalens Datakonsult AB a KDAB Group company <info@kdab.com>
    SPDX-FileContributor: Milian Wolff <milian.wolff@kdab.com>

   SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "dwarfdiecache.h"

#include "demangler.h"

#include <dwarf.h>

#include <cxxabi.h>

#include <cstring>

namespace {
enum class WalkResult
{
    Recurse,
    Skip,
    Return
};
template <typename Callback>
WalkResult walkDieTree(const Callback& callback, Dwarf_Die* die)
{
    auto result = callback(die);
    if (result != WalkResult::Recurse)
        return result;

    Dwarf_Die childDie;
    if (dwarf_child(die, &childDie) == 0) {
        result = walkDieTree(callback, &childDie);
        if (result == WalkResult::Return)
            return result;

        Dwarf_Die siblingDie;
        while (dwarf_siblingof(&childDie, &siblingDie) == 0) {
            result = walkDieTree(callback, &siblingDie);
            if (result == WalkResult::Return)
                return result;
            childDie = siblingDie;
        }
    }
    return WalkResult::Skip;
}

template <typename Callback>
void walkRanges(const Callback& callback, Dwarf_Die* die)
{
    Dwarf_Addr low = 0;
    Dwarf_Addr high = 0;
    Dwarf_Addr base = 0;
    ptrdiff_t rangeOffset = 0;
    while ((rangeOffset = dwarf_ranges(die, rangeOffset, &base, &low, &high)) > 0) {
        if (!callback(DwarfRange {low, high}))
            return;
    }
}

// see libdw_visit_scopes.c in elfutils
bool mayHaveScopes(Dwarf_Die* die)
{
    switch (dwarf_tag(die)) {
    /* DIEs with addresses we can try to match.  */
    case DW_TAG_compile_unit:
    case DW_TAG_module:
    case DW_TAG_lexical_block:
    case DW_TAG_with_stmt:
    case DW_TAG_catch_block:
    case DW_TAG_try_block:
    case DW_TAG_entry_point:
    case DW_TAG_inlined_subroutine:
    case DW_TAG_subprogram:
        return true;

    /* DIEs without addresses that can own DIEs with addresses.  */
    case DW_TAG_namespace:
    case DW_TAG_class_type:
    case DW_TAG_structure_type:
        return true;

    /* Other DIEs we have no reason to descend.  */
    default:
        break;
    }
    return false;
}

bool dieContainsAddress(Dwarf_Die* die, Dwarf_Addr address)
{
    bool contained = false;
    walkRanges(
        [&contained, address](DwarfRange range) {
            if (range.contains(address)) {
                contained = true;
                return false;
            }
            return true;
        },
        die);
    return contained;
}
}

const char* linkageName(Dwarf_Die* die)
{
    Dwarf_Attribute attr;
    Dwarf_Attribute* result = dwarf_attr_integrate(die, DW_AT_MIPS_linkage_name, &attr);
    if (!result)
        result = dwarf_attr_integrate(die, DW_AT_linkage_name, &attr);

    return result ? dwarf_formstring(result) : nullptr;
}

Dwarf_Die* specificationDie(Dwarf_Die* die, Dwarf_Die* dieMem)
{
    Dwarf_Attribute attr;
    if (dwarf_attr_integrate(die, DW_AT_specification, &attr))
        return dwarf_formref_die(&attr, dieMem);
    return nullptr;
}

/// prepend the names of all scopes that reference the @p die to @p name
void prependScopeNames(std::string& name, Dwarf_Die* die, tsl::robin_map<Dwarf_Off, std::string>& cache)
{
    Dwarf_Die dieMem;
    Dwarf_Die* scopes = nullptr;
    auto nscopes = dwarf_getscopes_die(die, &scopes);

    struct ScopesToCache
    {
        Dwarf_Off offset;
        std::size_t trailing;
    };
    std::vector<ScopesToCache> cacheOps;

    // skip scope for the die itself at the start and the compile unit DIE at end
    for (int i = 1; i < nscopes - 1; ++i) {
        auto scope = scopes + i;

        const auto scopeOffset = dwarf_dieoffset(scope);

        auto it = cache.find(scopeOffset);
        if (it != cache.end()) {
            name.insert(0, "::");
            name.insert(0, it->second);
            // we can stop, cached names are always fully qualified
            break;
        }

        if (auto scopeLinkageName = linkageName(scope)) {
            // prepend the fully qualified linkage name
            name.insert(0, "::");
            cacheOps.push_back({scopeOffset, name.size()});
            // we have to demangle the scope linkage name, otherwise we get a
            // mish-mash of mangled and non-mangled names
            name.insert(0, demangle(scopeLinkageName));
            // we can stop now, the scope is fully qualified
            break;
        }

        if (auto scopeName = dwarf_diename(scope)) {
            // prepend this scope's name, e.g. the class or namespace name
            name.insert(0, "::");
            cacheOps.push_back({scopeOffset, name.size()});
            name.insert(0, scopeName);
        }

        if (auto specification = specificationDie(scope, &dieMem)) {
            free(scopes);
            scopes = nullptr;
            cacheOps.push_back({scopeOffset, name.size()});
            cacheOps.push_back({dwarf_dieoffset(specification), name.size()});
            // follow the scope's specification DIE instead
            prependScopeNames(name, specification, cache);
            break;
        }
    }

    for (const auto& cacheOp : cacheOps)
        cache[cacheOp.offset] = name.substr(0, name.size() - cacheOp.trailing);

    free(scopes);
}

bool operator==(const Dwarf_Die& lhs, const Dwarf_Die& rhs)
{
    return lhs.addr == rhs.addr && lhs.cu == rhs.cu && lhs.abbrev == rhs.abbrev;
}

std::string qualifiedDieName(Dwarf_Die* die, tsl::robin_map<Dwarf_Off, std::string>& cache)
{
    // linkage names are fully qualified, meaning we can stop early then
    if (auto name = linkageName(die))
        return name;

    // otherwise do a more complex lookup that includes namespaces and other context information
    // this is important for inlined subroutines such as lambdas or std:: algorithms
    std::string name;
    if (auto dieName = dwarf_diename(die))
        name = dieName;

    // use the specification DIE which is within the DW_TAG_namespace
    Dwarf_Die dieMem;
    if (auto specification = specificationDie(die, &dieMem))
        die = specification;

    prependScopeNames(name, die, cache);

    return name;
}

std::string demangle(const std::string& mangledName)
{
    static Demangler demangler;
    return demangler.demangle(mangledName);
}

std::string absoluteSourcePath(const char* path, Dwarf_Die* cuDie)
{
    if (!path)
        return "";

    if (!cuDie || path[0] == '/')
        return path;

    Dwarf_Attribute attr;
    auto compDir = dwarf_formstring(dwarf_attr(cuDie, DW_AT_comp_dir, &attr));
    if (!compDir)
        return path;

    std::string ret;
    ret.reserve(static_cast<int>(strlen(compDir) + strlen(path) + 1));
    ret.append(compDir);
    ret.append("/");
    ret.append(path);
    return ret;
}

SourceLocation callSourceLocation(Dwarf_Die* die, Dwarf_Files* files, Dwarf_Die* cuDie)
{
    SourceLocation ret;

    Dwarf_Attribute attr;
    Dwarf_Word val = 0;

    const auto hasCallFile = dwarf_formudata(dwarf_attr(die, DW_AT_call_file, &attr), &val) == 0;
    if (hasCallFile) {
        ret.file = absoluteSourcePath(dwarf_filesrc(files, val, nullptr, nullptr), cuDie);
    }

    const auto hasCallLine = dwarf_formudata(dwarf_attr(die, DW_AT_call_line, &attr), &val) == 0;
    if (hasCallLine) {
        ret.line = static_cast<int>(val);
    }

    return ret;
}

std::vector<Dwarf_Die> findInlineScopes(Dwarf_Die* subprogram, Dwarf_Addr offset)
{
    std::vector<Dwarf_Die> scopes;
    walkDieTree(
        [offset, &scopes](Dwarf_Die* die) {
            if (dwarf_tag(die) != DW_TAG_inlined_subroutine)
                return WalkResult::Recurse;
            if (dieContainsAddress(die, offset)) {
                scopes.push_back(*die);
                return WalkResult::Recurse;
            }
            return WalkResult::Skip;
        },
        subprogram);
    return scopes;
}

SubProgramDie::SubProgramDie(Dwarf_Die die)
    : m_ranges {die, {}}
{
    walkRanges(
        [this](DwarfRange range) {
            m_ranges.ranges.push_back(range);
            return true;
        },
        &die);
}

CuDieRangeMapping::CuDieRangeMapping(Dwarf_Die cudie, Dwarf_Addr bias)
    : m_bias {bias}
    , m_cuDieRanges {cudie, {}}
{
    walkRanges(
        [this, bias](DwarfRange range) {
            m_cuDieRanges.ranges.push_back({range.low + bias, range.high + bias});
            return true;
        },
        &cudie);
}

SubProgramDie* CuDieRangeMapping::findSubprogramDie(Dwarf_Addr offset)
{
    if (m_subPrograms.empty())
        addSubprograms();

    auto it = std::find_if(m_subPrograms.begin(), m_subPrograms.end(),
                           [offset](const SubProgramDie& program) { return program.contains(offset); });
    if (it == m_subPrograms.end())
        return nullptr;

    return &(*it);
}

void CuDieRangeMapping::addSubprograms()
{
    walkDieTree(
        [this](Dwarf_Die* die) {
            if (!mayHaveScopes(die))
                return WalkResult::Skip;

            if (dwarf_tag(die) == DW_TAG_subprogram) {
                SubProgramDie program(*die);
                if (!program.isEmpty())
                    m_subPrograms.push_back(program);

                return WalkResult::Skip;
            }
            return WalkResult::Recurse;
        },
        cudie());
}

const std::string& CuDieRangeMapping::dieName(Dwarf_Die* die)
{
    const auto offset = dwarf_dieoffset(die);
    auto it = m_dieNameCache.find(offset);
    if (it == m_dieNameCache.end())
        it = m_dieNameCache.insert({offset, demangle(qualifiedDieName(die, m_dieNameCache))}).first;

    return it->second;
}

DwarfDieCache::DwarfDieCache(Dwfl_Module* mod)
{
    if (!mod)
        return;

    Dwarf_Die* die = nullptr;
    Dwarf_Addr bias = 0;
    while ((die = dwfl_module_nextcu(mod, die, &bias))) {
        CuDieRangeMapping cuDieMapping(*die, bias);
        if (!cuDieMapping.isEmpty())
            m_cuDieRanges.push_back(cuDieMapping);
    }
}

CuDieRangeMapping* DwarfDieCache::findCuDie(Dwarf_Addr addr)
{
    auto it = std::find_if(m_cuDieRanges.begin(), m_cuDieRanges.end(),
                           [addr](const CuDieRangeMapping& cuDieMapping) { return cuDieMapping.contains(addr); });
    if (it == m_cuDieRanges.end())
        return nullptr;

    return &(*it);
}
