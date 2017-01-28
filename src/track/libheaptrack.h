/*
 * Copyright 2014-2017 Milian Wolff <mail@milianw.de>
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

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*heaptrack_callback_t)();
typedef void (*heaptrack_callback_initialized_t)(FILE*);

void heaptrack_init(const char* outputFileName, heaptrack_callback_t initCallbackBefore,
                    heaptrack_callback_initialized_t initCallbackAfter, heaptrack_callback_t stopCallback);

void heaptrack_stop();

void heaptrack_malloc(void* ptr, size_t size);

void heaptrack_free(void* ptr);

void heaptrack_realloc(void* ptr_in, size_t size, void* ptr_out);

void heaptrack_invalidate_module_cache();

#ifdef __cplusplus
}
#endif
