/*
 * Copyright 2016-2017 Milian Wolff <mail@milianw.de>
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

#ifndef LOCATIONDATA_H
#define LOCATIONDATA_H

#include <QHashFunctions>
#include <QMetaType>

#include <util/indices.h>

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
