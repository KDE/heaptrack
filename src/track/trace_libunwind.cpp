/*
 * Copyright 2014-2019 Milian Wolff <mail@milianw.de>
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
 * @brief A libunwind based backtrace.
 */

#include "trace.h"

#include "util/libunwind_config.h"

#define UNW_LOCAL_ONLY
#include <libunwind.h>

#include <stdio.h>

void Trace::print()
{
#if LIBUNWIND_HAS_UNW_GETCONTEXT && LIBUNWIND_HAS_UNW_INIT_LOCAL
    unw_context_t context;
    unw_getcontext(&context);

    unw_cursor_t cursor;
    unw_init_local(&cursor, &context);

    int frameNr = 0;
    while (unw_step(&cursor)) {
        ++frameNr;
        unw_word_t ip = 0;
        unw_get_reg(&cursor, UNW_REG_IP, &ip);

        unw_word_t sp = 0;
        unw_get_reg(&cursor, UNW_REG_SP, &sp);

        char symbol[256] = {"<unknown>"};
        unw_word_t offset = 0;
        unw_get_proc_name(&cursor, symbol, sizeof(symbol), &offset);

        fprintf(stderr, "#%-2d 0x%016" PRIxPTR " sp=0x%016" PRIxPTR " %s + 0x%" PRIxPTR "\n", frameNr,
                static_cast<uintptr_t>(ip), static_cast<uintptr_t>(sp), symbol, static_cast<uintptr_t>(offset));
    }
#endif
}

void Trace::setup()
{
    // configure libunwind for better speed
    if (unw_set_caching_policy(unw_local_addr_space, UNW_CACHE_PER_THREAD)) {
        fprintf(stderr, "WARNING: Failed to enable per-thread libunwind caching.\n");
    }
#if LIBUNWIND_HAS_UNW_SET_CACHE_SIZE
    if (unw_set_cache_size(unw_local_addr_space, 1024, 0)) {
        fprintf(stderr, "WARNING: Failed to set libunwind cache size.\n");
    }
#endif
}

int Trace::unwind(void** data)
{
    return unw_backtrace(data, MAX_SIZE);
}
