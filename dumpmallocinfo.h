/*
 * Copyright 2013 Milian Wolff <mail@milianw.de>
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

#ifndef DUMPMALLOCINFO_H
#define DUMPMALLOCINFO_H
#include <stdio.h>

extern "C" {
void start_dump_malloc_info(unsigned int millisecond_interval);
void stop_dump_malloc_info();
}

class DumpMallocInfoOnStartup
{
public:
    DumpMallocInfoOnStartup();
    ~DumpMallocInfoOnStartup();
private:
    FILE* output;
};

static DumpMallocInfoOnStartup dumpMallocInfoOnStartup;

#endif // DUMPMALLOCINFO_H
