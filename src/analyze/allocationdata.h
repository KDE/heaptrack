/*
    SPDX-FileCopyrightText: 2016-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
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
