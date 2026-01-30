/*
    SPDX-FileCopyrightText: 2014-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "3rdparty/doctest.h"

#include "track/trace.h"
#include "track/tracetree.h"

#include "interpret/dwarfdiecache.h"

#include <elfutils/libdwelf.h>

#include <algorithm>
#include <future>
#include <thread>

#include <link.h>

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

bool bar(Trace& trace, int depth);

inline bool __attribute__((always_inline)) asdf(Trace& trace, int depth)
{
    return bar(trace, depth - 1);
}

inline bool __attribute__((always_inline)) foo(Trace& trace, int depth)
{
    return asdf(trace, depth);
}

bool __attribute__((noinline)) bar(Trace& trace, int depth)
{
    if (!depth) {
        return trace.fill(0);
    } else {
        return foo(trace, depth);
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

                    const std::lock_guard<std::mutex> guard(mutex);
                    uint32_t lastParent = 0;
                    auto index =
                        tree.index(trace, [k, j, leaf, &lastParent, &ipsToParent](uintptr_t ip, uint32_t parentIndex) {
                            // for larger k, the trace is known and thus we won't hit this branch
                            REQUIRE(k == 0);

                            REQUIRE(ip > 0);
                            REQUIRE((ip <= (j + 1) || ip == leaf));
                            REQUIRE(((!lastParent && !parentIndex) || parentIndex > lastParent));
                            REQUIRE(parentIndex <= ipsToParent.size());
                            lastParent = parentIndex;

                            ipsToParent.push_back({ip, parentIndex});
                            return true;
                        });
                    REQUIRE(index > lastIndex);
                    REQUIRE(index <= ipsToParent.size());

                    if (k == 0) {
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

struct CallbackData
{
    Dwfl* dwfl = nullptr;
    Dwfl_Module* mod = nullptr;
};
static int dl_iterate_phdr_dwfl_report_callback(struct dl_phdr_info* info, size_t /*size*/, void* data)
{
    const char* fileName = info->dlpi_name;
    if (!fileName || !fileName[0]) {
        auto callbackData = reinterpret_cast<CallbackData*>(data);
        callbackData->mod = dwfl_report_elf(callbackData->dwfl, "tst_trace", "/proc/self/exe", -1, info->dlpi_addr, false);
        REQUIRE(callbackData->mod);
    }

    return 0;
}

TEST_CASE ("symbolizing") {
    Trace trace;

    REQUIRE(bar(trace, 5));
    REQUIRE(trace.size() >= 6);

    Dwfl_Callbacks callbacks = {
        &dwfl_build_id_find_elf,
        &dwfl_standard_find_debuginfo,
        &dwfl_offline_section_address,
        nullptr,
    };

    auto dwfl = std::unique_ptr<Dwfl, void (*)(Dwfl*)>(dwfl_begin(&callbacks), &dwfl_end);
    REQUIRE(dwfl);

    dwfl_report_begin(dwfl.get());
    CallbackData data = { dwfl.get(), nullptr };
    dl_iterate_phdr(&dl_iterate_phdr_dwfl_report_callback, &data);
    dwfl_report_end(dwfl.get(), nullptr, nullptr);

    REQUIRE(data.mod);

    DwarfDieCache cache(data.mod);
    size_t j = 0;
    for (size_t i = 0; i < 6 + j; ++i) {
        auto addr = reinterpret_cast<Dwarf_Addr>(trace[i]);

        auto cuDie = cache.findCuDie(addr);
        REQUIRE(cuDie);

        auto offset = addr - cuDie->bias();
        auto die = cuDie->findSubprogramDie(offset);
        REQUIRE(die);

        auto dieName = cuDie->dieName(die->die());
        auto isDebugBuild = i == 0 && dieName == "Trace::unwind(void**)";
        if (i == 0 + j) {
            if (!isDebugBuild)
                REQUIRE(dieName == "Trace::fill(int)");
        } else {
            REQUIRE(dieName == "bar");
        }

        auto scopes = findInlineScopes(die->die(), offset);
        if (i <= 1 + j) {
            REQUIRE(scopes.size() == 0);
        } else {
            REQUIRE(scopes.size() == 2);

            Dwarf_Files* files = nullptr;
            dwarf_getsrcfiles(cuDie->cudie(), &files, nullptr);
            REQUIRE(files);

            REQUIRE(cuDie->dieName(&scopes[0]) == "foo");
            auto loc = callSourceLocation(&scopes[0], files, cuDie->cudie());
            // called from bar
            REQUIRE(loc.line == 52);

            REQUIRE(cuDie->dieName(&scopes[1]) == "asdf");
            loc = callSourceLocation(&scopes[1], files, cuDie->cudie());
            // called from foo
            REQUIRE(loc.line == 44);
        }

        if (isDebugBuild) {
            ++j;
        }
    }
}
