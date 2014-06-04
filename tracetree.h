/*
 * Copyright 2014 Milian Wolff <mail@milianw.de>
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

#ifndef TRACETREE_H
#define TRACETREE_H

#include <vector>
#include <algorithm>

#include "trace.h"

struct TraceEdge
{
    Trace::ip_t instructionPointer;
    // index associated to the backtrace up to this instruction pointer
    // the evaluation process can then reverse-map the index to the parent ip
    // to rebuild the backtrace from the bottom-up
    std::size_t index;
    // Unsorted list of children, assumed to be small
    std::vector<TraceEdge> children;
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
    /**
     * Index the backtrace and return the index of the last instruction pointer.
     */
    std::size_t index(const Trace& trace, FILE* out)
    {
        size_t index = 0;
        TraceEdge* parent = &m_root;
        for (int i = trace.size() - 1; i >= 0; --i) {
            const auto ip = trace[i];
            if (!ip) {
                continue;
            }
            auto it = std::find_if(parent->children.begin(), parent->children.end(), [ip] (const TraceEdge& l) {
                return l.instructionPointer == ip;
            });
            if (it == parent->children.end()) {
                index = m_index++;
                it = parent->children.insert(it, {ip, index, {}});
                fprintf(out, "i %lu %p %lu\n", index, ip, parent->index);
            }
            index = it->index;
            parent = &(*it);
        }
        return index;
    }

private:
    TraceEdge m_root = {0, 0, {}};
    std::size_t m_index = 1;
};

#endif // TRACETREE_H
