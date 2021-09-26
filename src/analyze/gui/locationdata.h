/*
    SPDX-FileCopyrightText: 2016-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#ifndef LOCATIONDATA_H
#define LOCATIONDATA_H

#include <QHashFunctions>
#include <QMetaType>

#include <util/indices.h>

#include <boost/functional/hash.hpp>

Q_DECLARE_METATYPE(ModuleIndex)
Q_DECLARE_METATYPE(FunctionIndex)
Q_DECLARE_METATYPE(FileIndex)

struct Symbol
{
    // function name
    FunctionIndex functionId;
    // path to dso / executable
    ModuleIndex moduleId;

    bool operator==(const Symbol& rhs) const
    {
        return functionId == rhs.functionId && moduleId == rhs.moduleId;
    }

    bool operator!=(const Symbol& rhs) const
    {
        return !operator==(rhs);
    }

    bool operator<(const Symbol& rhs) const
    {
        return std::tie(functionId, moduleId) < std::tie(rhs.functionId, rhs.moduleId);
    }

    bool isValid() const
    {
        return *this != Symbol {};
    }
};

Q_DECLARE_TYPEINFO(Symbol, Q_MOVABLE_TYPE);
Q_DECLARE_METATYPE(Symbol)

struct FileLine
{
    FileIndex fileId;
    int line;

    bool operator==(const FileLine& rhs) const
    {
        return fileId == rhs.fileId && line == rhs.line;
    }
};
Q_DECLARE_TYPEINFO(FileLine, Q_MOVABLE_TYPE);
Q_DECLARE_METATYPE(FileLine)

const QString& unresolvedFunctionName();

namespace std {
template <>
struct hash<Symbol>
{
    std::size_t operator()(const Symbol symbol) const
    {
        return boost::hash_value(std::tie(symbol.functionId.index, symbol.moduleId.index));
    }
};
}

inline uint qHash(const Symbol& symbol, uint seed = 0)
{
    QtPrivate::QHashCombine hash;
    seed = hash(seed, symbol.functionId);
    seed = hash(seed, symbol.moduleId);
    return seed;
}

inline uint qHash(const FileLine& location, uint seed = 0)
{
    QtPrivate::QHashCombine hash;
    seed = hash(seed, location.fileId);
    seed = hash(seed, location.line);
    return seed;
}

#endif // LOCATIONDATA_H
