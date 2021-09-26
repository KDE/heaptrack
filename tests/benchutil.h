/*
    SPDX-FileCopyrightText: 2020 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#pragma once

// source: https://www.youtube.com/watch?v=nXaxk27zwlk
inline void escape(void* p)
{
    asm volatile("" : : "g"(p) : "memory");
}

#ifdef __cplusplus
inline void escape(const void* p)
{
    escape(const_cast<void*>(p));
}
#endif

inline void clobber()
{
    asm volatile("" : : : "memory");
}
