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

    bool operator!=(const Symbol& rhs) const
    {
        return !operator==(rhs);
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

struct FileLine
{
    QString file;
    int line;

    bool operator==(const FileLine& rhs) const
    {
        return file == rhs.file && line == rhs.line;
    }

    bool operator<(const FileLine& rhs) const
    {
        return std::tie(file, line) < std::tie(rhs.file, rhs.line);
    }

    QString toString() const
    {
        return file.isEmpty() ? QStringLiteral("??") : (file + QLatin1Char(':') + QString::number(line));
    }
};
Q_DECLARE_TYPEINFO(FileLine, Q_MOVABLE_TYPE);
Q_DECLARE_METATYPE(FileLine)

inline QString unresolvedFunctionName()
{
    static QString msg = i18n("<unresolved function>");
    return msg;
}

inline uint qHash(const Symbol& symbol, uint seed_ = 0)
{
    size_t seed = seed_;
    boost::hash_combine(seed, qHash(symbol.symbol));
    boost::hash_combine(seed, qHash(symbol.binary));
    boost::hash_combine(seed, qHash(symbol.path));
    return seed;
}

inline uint qHash(const FileLine& location, uint seed_ = 0)
{
    size_t seed = seed_;
    boost::hash_combine(seed, qHash(location.file));
    boost::hash_combine(seed, location.line);
    return seed;
}

#endif // LOCATIONDATA_H
