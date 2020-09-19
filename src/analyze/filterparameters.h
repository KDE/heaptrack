/*
 * Copyright 2015-2020 Milian Wolff <mail@milianw.de>
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

#ifndef FILTERPARAMETERS_H
#define FILTERPARAMETERS_H

#include <cstdint>
#include <limits>

struct FilterParameters
{
    int64_t minTime = 0;
    int64_t maxTime = std::numeric_limits<int64_t>::max();
    bool isFiltered(int64_t totalTime) const
    {
        return minTime != 0 || maxTime < totalTime;
    }
};

#endif // FILTERPARAMETERS_H
