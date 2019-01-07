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

#ifndef ALLOCATIONDATA_H
#define ALLOCATIONDATA_H

#include <cstdint>

struct AllocationData
{
    // number of allocations
    int64_t allocations = 0;
    // number of temporary allocations
    int64_t temporary = 0;
    // amount of bytes leaked
    int64_t leaked = 0;
    // largest amount of bytes allocated
    int64_t peak = 0;

    void clearCost()
    {
        *this = {};
    }
};

inline bool operator==(const AllocationData& lhs, const AllocationData& rhs)
{
    return lhs.allocations == rhs.allocations && lhs.temporary == rhs.temporary && lhs.leaked == rhs.leaked
        && lhs.peak == rhs.peak;
}

inline bool operator!=(const AllocationData& lhs, const AllocationData& rhs)
{
    return !(lhs == rhs);
}

inline AllocationData& operator+=(AllocationData& lhs, const AllocationData& rhs)
{
    lhs.allocations += rhs.allocations;
    lhs.temporary += rhs.temporary;
    lhs.peak += rhs.peak;
    lhs.leaked += rhs.leaked;
    return lhs;
}

inline AllocationData& operator-=(AllocationData& lhs, const AllocationData& rhs)
{
    lhs.allocations -= rhs.allocations;
    lhs.temporary -= rhs.temporary;
    lhs.peak -= rhs.peak;
    lhs.leaked -= rhs.leaked;
    return lhs;
}

inline AllocationData operator+(AllocationData lhs, const AllocationData& rhs)
{
    return lhs += rhs;
}

inline AllocationData operator-(AllocationData lhs, const AllocationData& rhs)
{
    return lhs -= rhs;
}

#endif // ALLOCATIONDATA_H
