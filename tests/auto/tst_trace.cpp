/*
    SPDX-FileCopyrightText: 2014-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "3rdparty/doctest.h"

#include "track/trace.h"
#include "track/tracetree.h"

#include <algorithm>
#include <future>
#include <thread>

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
    SUBCASE("validate the trace size")
    {
        REQUIRE(trace.size() == expectedSize);
        REQUIRE(distance(trace.begin(), trace.end()) == trace.size());
    }
    SUBCASE("validate trace contents")
    {
        REQUIRE(find(trace.begin(), trace.end(), Trace::ip_t(0)) == trace.end());
    }
}
}

TEST_CASE ("getting backtrace traces") {
    Trace trace;
    validateTrace(trace, 0);

    SUBCASE("fill without skipping")
    {
        REQUIRE(trace.fill(0));
        const auto offset = trace.size();
        REQUIRE(offset > 1);
        validateTrace(trace, offset);

        SUBCASE("fill with skipping")
        {
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

TEST_CASE ("tracetree indexing") {
    TraceTree tree;

    std::mutex mutex;
    const auto numTasks = std::thread::hardware_concurrency();
    std::vector<std::future<void>> tasks(numTasks);

    struct IpToParent
    {
        uintptr_t ip;
        uint32_t parentIndex;
    };
    std::vector<IpToParent> ipsToParent;

    struct IndexedTrace
    {
        Trace trace;
        uint32_t index;
    };
    std::vector<IndexedTrace> traces;

    // fill the tree
    for (auto i = 0u; i < numTasks; ++i) {
        tasks[i] = std::async(std::launch::async, [&mutex, &tree, &ipsToParent, &traces, i]() {
            Trace trace;

            const auto leaf = uintptr_t((i + 1) * 100);

            for (int k = 0; k < 100; ++k) {
                uint32_t lastIndex = 0;
                for (uintptr_t j = 0; j < 32; ++j) {
                    trace.fillTestData(j, leaf);
                    REQUIRE(trace.size() == j + 1);

                    uint32_t lastParent = 0;
                    auto index = tree.index(
                        trace, [k, j, leaf, &lastParent, &ipsToParent, &mutex](uintptr_t ip, uint32_t parentIndex) {
                            // for larger k, the trace is known and thus we won't hit this branch
                            REQUIRE(k == 0);

                            REQUIRE(ip > 0);
                            REQUIRE((ip <= (j + 1) || ip == leaf));
                            REQUIRE(((!lastParent && !parentIndex) || parentIndex > lastParent));
                            lastParent = parentIndex;

                            const std::lock_guard<std::mutex> guard(mutex);
                            REQUIRE(parentIndex <= ipsToParent.size());
                            ipsToParent.push_back({ip, parentIndex});
                            return true;
                        });
                    REQUIRE(index > lastIndex);
                    {
                        const std::lock_guard<std::mutex> guard(mutex);
                        REQUIRE(index <= ipsToParent.size());
                    }

                    if (k == 0) {
                        const std::lock_guard<std::mutex> guard(mutex);
                        traces.push_back({trace, index});
                    }
                }
            }
        });
    }

    // wait for threads to finish
    for (auto& task : tasks) {
        task.get();
    }

    // verify that we can rebuild the traces
    for (const auto& trace : traces) {
        uint32_t index = trace.index;
        int i = 0;
        while (index) {
            REQUIRE(i < trace.trace.size());
            REQUIRE(index > 0);
            REQUIRE(index <= ipsToParent.size());

            auto map = ipsToParent[index - 1];
            REQUIRE(map.ip == reinterpret_cast<uintptr_t>(trace.trace[i]));

            index = map.parentIndex;
            ++i;
        }
    }
}
