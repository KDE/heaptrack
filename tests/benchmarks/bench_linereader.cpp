/*
 * Copyright 2020 Milian Wolff <mail@milianw.de>
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

#include <src/util/linereader.h>

#include <iostream>
#include <sstream>
#include <string>

int main()
{
    std::string contents;
    contents.reserve(5400000);
    for (int i = 0; i < 100000; ++i) {
        contents.append("0 1 2 3\n");
        contents.append("102 345 678 9ab\n");
        contents.append("102345 6789ab cdef01 23456789\n");
    }

    uint64_t ret = 0;
    for (int i = 0; i < 1000; ++i) {
        std::istringstream in(contents);
        LineReader reader;
        while (reader.getLine(in)) {
            uint64_t hex;
            while (reader.readHex(hex)) {
                ret += hex;
            }
        }
    }

    std::cout << ret << '\n';
    return 0;
}
