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

#include <QMetaType>
#include <QString>

#include <memory>

#include <boost/functional/hash.hpp>

#include <KLocalizedString>

struct Symbol
{
    Symbol(const QString& symbol = {}, const QString& binary = {}, const QString& path = {})
        : symbol(symbol)
        , binary(binary)
        , path(path)
    {
    }

    // function name
    QString symbol;
    // dso / executable name
    QString binary;
    // path to dso / executable
    QString path;

    bool operator==(const Symbol& rhs) const
    {
        return std::tie(symbol, binary, path) == std::tie(rhs.symbol, rhs.binary, rhs.path);
    }

    bool operator<(const Symbol& rhs) const
    {
        return std::tie(symbol, binary, path) < std::tie(rhs.symbol, rhs.binary, rhs.path);
    }

    bool isValid() const
    {
        return !symbol.isEmpty() || !binary.isEmpty() || !path.isEmpty();
    }
};

Q_DECLARE_TYPEINFO(Symbol, Q_MOVABLE_TYPE);
Q_DECLARE_METATYPE(Symbol)

struct LocationData
{
    using Ptr = std::shared_ptr<LocationData>;

    Symbol symbol;
    QString file;
    int line;

    bool operator==(const LocationData& rhs) const
    {
        return symbol == rhs.symbol && file == rhs.file && line == rhs.line;
    }

    bool operator<(const LocationData& rhs) const
    {
        return std::tie(symbol, file, line) < std::tie(rhs.symbol, rhs.file, rhs.line);
    }

    QString fileLine() const
    {
        return file.isEmpty() ? QStringLiteral("??") : (file + QLatin1Char(':') + QString::number(line));
    }
};
Q_DECLARE_TYPEINFO(LocationData, Q_MOVABLE_TYPE);
Q_DECLARE_METATYPE(LocationData::Ptr)

inline QString unresolvedFunctionName()
{
    return i18n("<unresolved function>");
}

inline bool operator<(const LocationData::Ptr& lhs, const LocationData& rhs)
{
    return *lhs < rhs;
}

inline uint qHash(const Symbol& symbol, uint seed_ = 0)
{
    size_t seed = seed_;
    boost::hash_combine(seed, qHash(symbol.symbol));
    boost::hash_combine(seed, qHash(symbol.binary));
    boost::hash_combine(seed, qHash(symbol.path));
    return seed;
}

inline uint qHash(const LocationData& location, uint seed_ = 0)
{
    size_t seed = seed_;
    boost::hash_combine(seed, qHash(location.symbol));
    boost::hash_combine(seed, qHash(location.file));
    boost::hash_combine(seed, location.line);
    return seed;
}

inline uint qHash(const LocationData::Ptr& location, uint seed = 0)
{
    return location ? qHash(*location, seed) : seed;
}

#endif // LOCATIONDATA_H
