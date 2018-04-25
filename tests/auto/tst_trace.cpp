/*
 * Copyright 2014-2017 Milian Wolff <mail@milianw.de>
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

#include "3rdparty/catch.hpp"
#include "track/trace.h"

#include <algorithm>

using namespace std;

namespace {
bool __attribute__((noinline)) fill(Trace& trace, int depth, int skip)
{
    if (!depth) {
        return trace.fill(skip);
    } else {
        return fill(trace, depth - 1, skip);
    }
}

void validateTrace(const Trace& trace, int expectedSize)
{
    SECTION ("validate the trace size") {
        REQUIRE(trace.size() == expectedSize);
        REQUIRE(distance(trace.begin(), trace.end()) == trace.size());
    }
    SECTION ("validate trace contents") {
        REQUIRE(find(trace.begin(), trace.end(), Trace::ip_t(0)) == trace.end());
    }
}
}

TEST_CASE ("getting backtrace traces", "[trace]") {
    Trace trace;
    validateTrace(trace, 0);

    SECTION ("fill without skipping") {
        REQUIRE(trace.fill(0));
        const auto offset = trace.size();
        REQUIRE(offset > 1);
        validateTrace(trace, offset);

        SECTION ("fill with skipping") {
            for (auto skip : {0, 1, 2}) {
                for (int i = 0; i < 2 * Trace::MAX_SIZE; ++i) {
                    REQUIRE(fill(trace, i, skip));
                    const auto expectedSize = min(i + offset + 1 - skip, static_cast<int>(Trace::MAX_SIZE) - skip);
                    validateTrace(trace, expectedSize);
                }
            }
        }
    }
}
