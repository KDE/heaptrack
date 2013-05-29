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

#include <iostream>
#include <unistd.h>

using namespace std;

int main(int argc, char **argv)
{
    cerr << "This is just a test utility. To use this debug utility, run your app like this:" << endl
         << endl
         << "  DUMP_MALLOC_INFO_INTERVAL=100 LD_PRELOAD=./path/to/libdumpmallocinfo.cpp yourapp" << endl
         << endl
         << "The above will output the XML malloc info every 100ms." << endl;

    srand(0);
    for(int i = 0; i < 10000; ++i) {
        delete new int;
        new int[rand() % 100];
        malloc(rand() % 1000);
    }
    return 0;
}
