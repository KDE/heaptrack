/*
    SPDX-FileCopyrightText: 2014-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#ifndef TRACETREE_H
#define TRACETREE_H

/**
 * @file tracetree.h
 * @brief Efficiently combine and store the data of multiple Traces.
 */

#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
#include <vector>

#include <cassert>

#include "trace.h"

#define USE_MUTEX 0

#if USE_MUTEX
#include <memory>
#endif

struct SpinLock
{
#if USE_MUTEX
    SpinLock()
        : mutex(std::make_unique<std::mutex>())
    {
    }
#else
    SpinLock() = default;
#endif

    // must not be moved when locked
    SpinLock(SpinLock&& rhs)
    {
#if USE_MUTEX
        std::swap(mutex, rhs.mutex);
#else
        assert(!rhs.locked);
        assert(!locked);
#endif
    }
    SpinLock& operator=(SpinLock&& rhs)
    {
#if USE_MUTEX
        std::swap(mutex, rhs.mutex);
#else
        assert(!rhs.locked);
        assert(!locked);
#endif
        return *this;
    }

    void lock()
    {
#if USE_MUTEX
        mutex->lock();
#else
        for (;;) {
            if (!locked.exchange(true, std::memory_order_acquire)) {
                break;
            }
            while (locked.load(std::memory_order_relaxed)) {
                __builtin_ia32_pause();
            }
        }
#endif
    }

    void unlock()
    {
#if USE_MUTEX
        mutex->unlock();
#else
        assert(locked);
        locked.store(false, std::memory_order_release);
#endif
    }

#if USE_MUTEX
    std::unique_ptr<std::mutex> mutex;
#else
    std::atomic<bool> locked {false};
#endif
};

struct TraceEdge
{
    TraceEdge(Trace::ip_t ip, uint32_t index)
        : instructionPointer(ip)
        , index(index)
    {
    }

    Trace::ip_t instructionPointer;
    // index associated to the backtrace up to this instruction pointer
    // the evaluation process can then reverse-map the index to the parent ip
    // to rebuild the backtrace from the bottom-up
    uint32_t index;
    // sorted list of children, assumed to be small
    std::vector<TraceEdge*> children;
    SpinLock lock;
};

/**
 * Top-down tree of backtrace instruction pointers.
 *
 * This is supposed to be a memory efficient storage of all instruction pointers
 * ever encountered in any backtrace.
 */
class TraceTree
{
public:
    void clear()
    {
        m_root.children.clear();
        m_index = 1;
    }

    /**
     * Index the data in @p trace and return the index of the last instruction
     * pointer.
     *
     * Unknown instruction pointers will be handled by the @p callback
     */
    template <typename Fun>
    uint32_t index(const Trace& trace, Fun callback)
    {
        uint32_t index = 0;
        TraceEdge* parent = &m_root;

        for (int i = trace.size() - 1; i >= 0; --i) {
            const auto ip = trace[i];
            if (!ip) {
                continue;
            }

            std::lock_guard<SpinLock> guard(parent->lock);
#if !USE_MUTEX
            assert(parent->lock.locked);
#endif
            auto it =
                std::lower_bound(parent->children.begin(), parent->children.end(), ip,
                                 [](const TraceEdge* l, const Trace::ip_t ip) { return l->instructionPointer < ip; });
            if (it == parent->children.end() || (*it)->instructionPointer != ip) {
                std::lock_guard<SpinLock> edgesGuard(m_edgeLock);
                index = m_index++;
                m_edges.emplace_back(ip, index);
                it = parent->children.emplace(it, &m_edges.back());
                if (!callback(reinterpret_cast<uintptr_t>(ip), parent->index)) {
                    return 0;
                }
            }
            index = (*it)->index;

            parent = *it;
        }
        return index;
    }

private:
    SpinLock m_edgeLock;
    std::deque<TraceEdge> m_edges;
    TraceEdge m_root = {0, 0};
    uint32_t m_index = 1;
};

#endif // TRACETREE_H
