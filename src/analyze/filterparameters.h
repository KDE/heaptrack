/*
    SPDX-FileCopyrightText: 2015-2020 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#ifndef FILTERPARAMETERS_H
#define FILTERPARAMETERS_H

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

struct FilterParameters
{
    int64_t minTime = 0;
    int64_t maxTime = std::numeric_limits<int64_t>::max();
    std::vector<std::string> suppressions;
    bool disableEmbeddedSuppressions = false;
    bool disableBuiltinSuppressions = false;
    bool isFilteredByTime(int64_t totalTime) const
    {
        return minTime != 0 || maxTime < totalTime;
    }
};

#endif // FILTERPARAMETERS_H
