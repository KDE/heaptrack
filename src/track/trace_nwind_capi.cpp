/*
    SPDX-FileCopyrightText: 2014-2019 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

/**
 * @brief A libunwind based backtrace.
 */

#include "trace.h"

#include <cstdio>

#include <nwind_capi.h>

namespace {
struct AddressSpace
{
    AddressSpace()
        : handle(nwind_create_local_address_space())
    {
        nwind_local_address_space_use_shadow_stack(handle, 1);
    }

    ~AddressSpace()
    {
        nwind_free_local_address_space(handle);
    }

    nwind_local_address_space* const handle;
};

nwind_local_address_space* addressSpace()
{
    static AddressSpace addressSpace;
    return addressSpace.handle;
}

struct UnwindContext
{
    UnwindContext()
        : handle(nwind_create_local_unwind_context())
    {
    }

    ~UnwindContext()
    {
        nwind_free_local_unwind_context(handle);
    }

    nwind_local_unwind_context* const handle;
};

nwind_local_unwind_context* unwindContext()
{
    static thread_local UnwindContext unwindContext;
    return unwindContext.handle;
}
}

void Trace::print()
{
    Trace trace;
    if (!trace.fill(1)) {
        return;
    }

    unsigned frameNr = 0;
    for (auto ip : trace) {
        ++frameNr;
        fprintf(stderr, "#%-2u %p\n", frameNr, ip);
    }
}

void Trace::setup()
{
}

void Trace::invalidateModuleCache()
{
    nwind_reload_local_address_space(addressSpace());
}

int Trace::unwind(void** data)
{
    return nwind_local_backtrace(addressSpace(), unwindContext(), data, MAX_SIZE);
}
