/*
    SPDX-FileCopyrightText: 2019 Volodymyr Nikolaichuk <nikolaychuk.volodymyr@gmail.com>

    SPDX-License-Identifier: LGPL-2.1-or-later
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

void Trace::invalidateModuleCache()
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
