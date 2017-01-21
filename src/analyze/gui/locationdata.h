/*
 * Copyright 2016-2017 Milian Wolff <mail@milianw.de>
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

#ifndef LOCATIONDATA_H
#define LOCATIONDATA_H

#include <QString>

#include <memory>

struct LocationData
{
    using Ptr = std::shared_ptr<LocationData>;

    QString function;
    QString file;
    QString module;
    int line;

    bool operator==(const LocationData& rhs) const
    {
        return function == rhs.function
            && file == rhs.file
            && module == rhs.module
            && line == rhs.line;
    }

    bool operator<(const LocationData& rhs) const
    {
        int i = function.compare(rhs.function);
        if (!i) {
            i = file.compare(rhs.file);
        }
        if (!i) {
            i = line < rhs.line ? -1 : (line > rhs.line);
        }
        if (!i) {
            i = module.compare(rhs.module);
        }
        return i < 0;
    }
};
Q_DECLARE_TYPEINFO(LocationData, Q_MOVABLE_TYPE);
Q_DECLARE_METATYPE(LocationData::Ptr)

inline bool operator<(const LocationData::Ptr& lhs, const LocationData& rhs)
{
    return *lhs < rhs;
}

#endif // LOCATIONDATA_H
