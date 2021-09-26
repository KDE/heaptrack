/*
    SPDX-FileCopyrightText: 2020 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
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
