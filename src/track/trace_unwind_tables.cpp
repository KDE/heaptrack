/*
 * Copyright 2019 Volodymyr Nikolaichuk <nikolaychuk.volodymyr@gmail.com>
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

/**
 * @brief A unwind-tables based backtrace.
 */

#include "trace.h"

#include <cstdint>
#include <cstdio>
#include <unwind.h>

namespace {

struct backtrace
{
    void** data = nullptr;
    int ctr = 0;
    int max_size = 0;
};

_Unwind_Reason_Code unwind_backtrace_callback(struct _Unwind_Context* context, void* arg)
{
    backtrace* trace = static_cast<backtrace*>(arg);

    uintptr_t pc = _Unwind_GetIP(context);
    if (pc && trace->ctr < trace->max_size - 1) {
        trace->data[trace->ctr++] = (void*)(pc);
    }

    return _URC_NO_REASON;
}

}

void Trace::setup()
{
}

void Trace::print()
{
    Trace trace;
    trace.fill(1);
    for (auto ip : trace) {
        fprintf(stderr, "%p\n", ip);
    }
}

int Trace::unwind(void** data)
{
    backtrace trace;
    trace.data = data;
    trace.max_size = MAX_SIZE;

    _Unwind_Backtrace(unwind_backtrace_callback, &trace);
    return trace.ctr;
}
