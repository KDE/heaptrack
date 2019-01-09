/*
 * Copyright 2018 Milian Wolff <mail@milianw.de>
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

#include "3rdparty/catch.hpp"

#include "util/linereader.h"
#include "util/linewriter.h"

#include "tempfile.h"

#include <limits>

using namespace std;

constexpr uint64_t operator"" _u64(unsigned long long v)
{
    return static_cast<uint64_t>(v);
}

TEST_CASE ("write data", "[write]") {
    TempFile file;
    REQUIRE(file.open());

    LineWriter writer(file.fd);
    REQUIRE(writer.canWrite());
    REQUIRE(writer.write("hello world\n"));
    REQUIRE(writer.write("%d %x\n", 42, 42));
    REQUIRE(writer.writeHexLine('t', 0u, 0ul, 1u, 1ul, 15u, 15ul, 16u, 16ul));
    REQUIRE(writer.writeHexLine('u', std::numeric_limits<uint32_t>::max() - 1, std::numeric_limits<uint32_t>::max()));
    REQUIRE(writer.writeHexLine('l', std::numeric_limits<uint64_t>::max() - 1, std::numeric_limits<uint64_t>::max()));

    REQUIRE(file.readContents().empty());

    REQUIRE(writer.flush());

    const string expectedContents = "hello world\n"
                                    "42 2a\n"
                                    "t 0 0 1 1 f f 10 10\n"
                                    "u fffffffe ffffffff\n"
                                    "l fffffffffffffffe ffffffffffffffff\n";

    REQUIRE(file.readContents() == expectedContents);
}

TEST_CASE ("buffered write", "[write]") {
    TempFile file;
    REQUIRE(file.open());

    LineWriter writer(file.fd);
    REQUIRE(writer.canWrite());
    string expectedContents;
    for (unsigned i = 0; i < 10000; ++i) {
        REQUIRE(writer.write("%d %x\n", 42, 42));
        expectedContents += "42 2a\n";
        if (i % 1000 == 0) {
            const string longString(LineWriter::BUFFER_CAPACITY * 2, '*');
            REQUIRE(writer.write(longString));
            expectedContents += longString;
        }
    }
    for (unsigned i = 0; i < LineWriter::BUFFER_CAPACITY * 2; ++i) {
        const string longString(i, '*');
        REQUIRE(writer.write(longString));
        expectedContents += longString;
    }
    REQUIRE(expectedContents.size() > LineWriter::BUFFER_CAPACITY);
    REQUIRE(writer.flush());

    REQUIRE(file.readContents() == expectedContents);
}

TEST_CASE ("buffered writeHex", "[write]") {
    TempFile file;
    REQUIRE(file.open());

    LineWriter writer(file.fd);
    REQUIRE(writer.canWrite());
    string expectedContents;
    for (unsigned i = 0; i < 10000; ++i) {
        REQUIRE(writer.writeHexLine('t', 0x123u, 0x456u));
        expectedContents += "t 123 456\n";
    }
    REQUIRE(expectedContents.size() > LineWriter::BUFFER_CAPACITY);
    REQUIRE(writer.flush());

    REQUIRE(file.readContents() == expectedContents);
}

TEST_CASE ("write flush", "[write]") {
    TempFile file;
    REQUIRE(file.open());

    LineWriter writer(file.fd);
    REQUIRE(writer.canWrite());

    string data1(LineWriter::BUFFER_CAPACITY - 10, '#');
    REQUIRE(writer.write(data1.c_str()));
    // not yet written
    REQUIRE(file.readContents().empty());

    // NOTE: while this data would fit,
    //       snprintf used by the writer tries to append a \0 too which doesn't fit
    string data2(10, '+');
    REQUIRE(writer.write(data2.c_str()));
    // so the above flushes, but only the first chunk
    REQUIRE(file.readContents() == data1);

    writer.flush();
    REQUIRE(file.readContents() == data1 + data2);
}

TEST_CASE ("read line 64bit", "[read]") {
    const string contents =
        "m /tmp/KDevelop-5.2.1-x86_64/usr/lib/libKF5Completion.so.5 7f48beedc00 0 36854 236858 2700\n";
    stringstream stream(contents);

    LineReader reader;
    REQUIRE(reader.getLine(stream));
    REQUIRE(reader.line()
            == "m /tmp/KDevelop-5.2.1-x86_64/usr/lib/libKF5Completion.so.5 7f48beedc00 0 36854 236858 2700");
    REQUIRE(reader.mode() == 'm');

    string module;
    REQUIRE(reader >> module);
    REQUIRE(module == "/tmp/KDevelop-5.2.1-x86_64/usr/lib/libKF5Completion.so.5");

    for (auto expected : {0x7f48beedc00_u64, 0x0_u64, 0x36854_u64, 0x236858_u64, 0x2700_u64}) {
        uint64_t addr = 0;
        REQUIRE(reader >> addr);
        REQUIRE(addr == expected);
    }

    uint64_t x = 0;
    REQUIRE(!(reader >> x));
    REQUIRE(!(reader >> module));
}

TEST_CASE ("read line 32bit", "[read]") {
    const string contents = "t 4 3\n"
                            "a 11c00 4\n"
                            "+ 0\n";
    stringstream stream(contents);
    LineReader reader;
    uint32_t idx = 0;

    REQUIRE(reader.getLine(stream));
    REQUIRE(reader.line() == "t 4 3");
    REQUIRE(reader.mode() == 't');
    REQUIRE(reader >> idx);
    REQUIRE(idx == 0x4);
    REQUIRE(reader >> idx);
    REQUIRE(idx == 0x3);
    REQUIRE(!(reader >> idx));

    REQUIRE(reader.getLine(stream));
    REQUIRE(reader.line() == "a 11c00 4");
    REQUIRE(reader.mode() == 'a');
    REQUIRE(reader >> idx);
    REQUIRE(idx == 0x11c00);
    REQUIRE(reader >> idx);
    REQUIRE(idx == 0x4);
    REQUIRE(!(reader >> idx));

    REQUIRE(reader.getLine(stream));
    REQUIRE(reader.line() == "+ 0");
    REQUIRE(reader.mode() == '+');

    REQUIRE(reader >> idx);
    REQUIRE(idx == 0x0);
    REQUIRE(!(reader >> idx));
}
