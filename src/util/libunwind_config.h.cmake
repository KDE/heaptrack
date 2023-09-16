/*
    SPDX-FileCopyrightText: 2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#ifndef LIBUNWIND_CONFIG_H
#define LIBUNWIND_CONFIG_H

#cmakedefine01 LIBUNWIND_HAS_UNW_BACKTRACE

#cmakedefine01 LIBUNWIND_HAS_UNW_BACKTRACE_SKIP

#cmakedefine01 LIBUNWIND_HAS_UNW_GETCONTEXT

#cmakedefine01 LIBUNWIND_HAS_UNW_INIT_LOCAL

#cmakedefine01 LIBUNWIND_HAS_UNW_SET_CACHE_SIZE

#cmakedefine01 LIBUNWIND_HAS_UNW_CACHE_PER_THREAD

#endif // LIBUNWIND_CONFIG_H

