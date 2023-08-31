/*
    SPDX-FileCopyrightText: 2023 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#pragma once

// TODO: c++17 use [[maybe_unused]]
#ifdef __GNUC__
#define POTENTIALLY_UNUSED __attribute__((unused))
#else
#define POTENTIALLY_UNUSED
#endif
