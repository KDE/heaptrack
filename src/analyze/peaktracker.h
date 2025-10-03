/*
    SPDX-FileCopyrightText: 2025-2025 Lennnox Shou-Hao Ho <lennoxhoe@gmail.com>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#pragma once

#include <algorithm>
#include <array>
#include <memory>

#include "accumulatedtracedata.h"

/// @brief Helper class to efficiently track and record peak memory information.
///
/// High level idea:
/// Given a non-seekable stream of decoded allocation "events" (A = alloc, D = dealloc)
///
///            A1 A2 D1 A3 D2 D3 A4 A5 A6 A7 D4 D5........
///
/// We treat this stream as a series of snippets of some fixed size (s_allocEventsCapacity)
///
///        [A1 A2 D1] [A3 D2 D3] [A4 A5 A6] [A7 D4 D5] ........
///
/// Each snippet starts with a full copy of all allocations.leaked values.
/// In other words, the current memory "snapshot" by allocations
///
/// Each snippet then stores its associated events in order without optimising for fast
/// access - as just a series of AllocationInfoIndex's (See PeakTracker::TraceSnippet)
///
/// As we process allocation events in the core parser loop, we build up the current snippet
/// - We do not keep around any previous snippet except for the peak snippet (more on this later)
/// - As we record events, we check if the a new memory peak is observed.
///   If so, record the (local) time index on the snippet
/// - When the current snippet is full, check if it contains a higher peak than the currently recorded
///   peak snippet, replace if that is the case
///
/// Populating allocations.peak is deferred until the end of the core parsing loop.
/// To do this, we "replay" the peak snippet, starting with the "snapshotted" memory values,
/// and process previously recorded allocation events in order until the peak time index.
///
/// This approach works by amortising both the memory & runtime cost of taking a complete "snapshot"
/// over every s_allocEventsCapacity allocation events.
/// It also amortises the runtime cost of compiling the final allocations.peak values.
class PeakTracker
{
    // 128MB might actually be a bit overkill, but compared
    // to overall memory usage of the GUI, it's not really that much.
    static constexpr std::size_t s_peakTrackingMaxOverhead = 128 * 1024 * 1024;

    /// @brief Storage for a snippet of allocation events
    class TraceSnippet
    {
        // Div 2 because we'll be keeping around up to 2 buffers at any time
        static constexpr std::size_t s_allocEventsCapacity =
            s_peakTrackingMaxOverhead / sizeof(AllocationInfoIndex) / 2;
        static_assert(s_allocEventsCapacity > 0);

        const AccumulatedTraceData& m_trace;

        std::int64_t m_peakTime = 0;
        std::int64_t m_peakMem = 0;
        std::size_t m_peakIdx = 0; // If idx == 0, look at m_startingAllocations.
                                   // Otherwise, should replay up to and including m_allocEvents[idx-1]

        std::vector<std::int64_t> m_startingAllocations; // starting allocs for this snippet

        std::size_t m_numAllocEvents = 0;
        std::array<AllocationInfoIndex, s_allocEventsCapacity> m_allocEvents;
        std::vector<bool> m_isAlloc;

    public:
        TraceSnippet(const AccumulatedTraceData& trace)
            : m_trace {trace}
            , m_isAlloc(s_allocEventsCapacity, false)
        {
            reset();
        }

        void reset()
        {
            m_peakTime = m_trace.parsingState.timestamp;
            m_peakMem = m_trace.totalCost.leaked;
            m_peakIdx = 0;

            m_startingAllocations.resize(m_trace.allocations.size());

            std::transform(m_trace.allocations.begin(), m_trace.allocations.end(), m_startingAllocations.begin(),
                           [](const auto& allocation) { return allocation.leaked; });

            m_numAllocEvents = 0;
        }

        bool isFull() const noexcept
        {
            return m_numAllocEvents == s_allocEventsCapacity;
        }

        void recordEvent(AllocationInfoIndex allocInfoIdx, bool isAlloc)
        {
            assert(!isFull());

            m_allocEvents[m_numAllocEvents] = allocInfoIdx;
            m_isAlloc[m_numAllocEvents] = isAlloc;
            ++m_numAllocEvents;

            if (m_trace.totalCost.leaked > m_peakMem) {
                // Found new peak
                m_peakTime = m_trace.parsingState.timestamp;
                m_peakMem = m_trace.totalCost.leaked;
                m_peakIdx = m_numAllocEvents;
            }
        }

        auto peakTime() const noexcept
        {
            return m_peakTime;
        }
        auto peakMem() const noexcept
        {
            return m_peakMem;
        }

        auto peakAllocations() const
        {
            auto peakAllocations = m_startingAllocations;

            if (m_peakIdx == 0) {
                return peakAllocations;
            }

            // Replay events until peak idx
            for (std::size_t idx = 0; idx < m_peakIdx; ++idx) {
                const auto allocInfoIdx = m_allocEvents[idx];
                const auto& alloc_info = m_trace.allocationInfos[allocInfoIdx.index];
                const auto allocIdx = alloc_info.allocationIndex;

                if (allocIdx.index >= peakAllocations.size()) {
                    // New allocations could have been introduced since
                    // the start of this snippet
                    peakAllocations.resize(allocIdx.index + 1, 0);
                }

                peakAllocations[allocIdx.index] += m_isAlloc[idx] ? alloc_info.size : -alloc_info.size;
            }
            return peakAllocations;
        }
    };

    // Keep track of a moving window of allocation state
    // Always hang on to the peak window
    std::unique_ptr<TraceSnippet> m_peakTraceSnippet;
    std::unique_ptr<TraceSnippet> m_currTraceSnippet;

public:
    PeakTracker(const AccumulatedTraceData& trace)
        : m_peakTraceSnippet {std::make_unique<TraceSnippet>(trace)}
        , m_currTraceSnippet {std::make_unique<TraceSnippet>(trace)}
    {
    }

    void recordEvent(AllocationInfoIndex allocInfoIdx, bool isAlloc)
    {
        if (m_currTraceSnippet->isFull()) {
            finalize();
        } else {
            m_currTraceSnippet->recordEvent(allocInfoIdx, isAlloc);
        }
    }

    void finalize()
    {
        if (m_currTraceSnippet->peakMem() > m_peakTraceSnippet->peakMem()) {
            m_peakTraceSnippet.swap(m_currTraceSnippet);
        }
        m_currTraceSnippet->reset();
    }

    auto peakTime() const noexcept
    {
        return m_peakTraceSnippet->peakTime();
    }
    auto peakAllocations() const
    {
        return m_peakTraceSnippet->peakAllocations();
    }
};
