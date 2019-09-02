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

#ifndef LINEWRITER_H
#define LINEWRITER_H

#include <algorithm>
#include <iterator>
#include <memory>
#include <type_traits>

#include <cassert>
#include <climits>
#include <cmath>
#include <string>
#include <cstring>

#include <errno.h>
#include <unistd.h>

/**
 * Custom buffered I/O writer for high performance and signal safety
 * See e.g.: https://bugs.kde.org/show_bug.cgi?id=393387
 */
class LineWriter
{
public:
    enum
    {
        BUFFER_CAPACITY = PIPE_BUF
    };

    LineWriter(int fd)
        : fd(fd)
        , buffer(new char[BUFFER_CAPACITY])
    {
        memset(buffer.get(), 0, BUFFER_CAPACITY);
    }

    ~LineWriter()
    {
        close();
    }

    /**
     * write an arbitrarily formatted string to the buffer
     */
    template <typename... T>
    bool write(const char* fmt, T... args)
    {
        enum
        {
            FirstTry,
            SecondTry
        };
        for (auto i : {FirstTry, SecondTry}) {
            const auto available = availableSpace();
            int ret = snprintf(out(), available, fmt, args...);

            if (ret < 0) {
                // snprintf failure
                return false;
            } else if (static_cast<unsigned>(ret) < available) {
                // success
                bufferSize += ret;
                return true;
            }

            // message didn't fit into available space
            if (i == SecondTry || static_cast<unsigned>(ret) > BUFFER_CAPACITY) {
                // message doesn't fit into BUFFER_CAPACITY - this should never happen
                assert(false && "message doesn't fit into buffer");
                errno = EFBIG;
                return false;
            }

            if (!flush()) {
                // write failed to flush
                return false;
            } // else try again after flush
        }

        // the loop above should have returned already
        __builtin_unreachable();
        return false;
    }

    /**
     * write an arbitrary string literal to the buffer
     */
    bool write(const char* line)
    {
        // TODO: could be optimized to use strncpy or similar, but this is rarely called
        return write("%s", line);
    }

    /**
     * write an arbitrary string literal to the buffer
     */
    bool write(const std::string& line)
    {
        const auto length = line.length();
        if (availableSpace() < length) {
            if (!flush()) {
                return false;
            }
            if (availableSpace() < length) {
                int ret = 0;
                do {
                    ret = ::write(fd, line.c_str(), length);
                } while (ret < 0 && errno == EINTR);
                return ret >= 0;
            }
        }
        memcpy(out(), line.c_str(), length);
        bufferSize += length;
        return true;
    }

    /**
     * write one of the common heaptrack output lines to the buffer
     *
     * @arg type char that identifies the type of the line
     * @arg args are all printed as hex numbers without leading 0x prefix
     *
     * e.g.: i 561072a1cf63 1 1c 18 70
     */
    template <typename... T>
    bool writeHexLine(const char type, T... args)
    {
        constexpr const int numArgs = sizeof...(T);
        constexpr const int maxHexCharsPerArg = 16; // 2^64 == 16^16
        constexpr const int maxCharsForArgs = numArgs * maxHexCharsPerArg;
        constexpr const int spaceCharsForArgs = numArgs;
        constexpr const int otherChars = 2; // type char and newline at end
        constexpr const int totalMaxChars = otherChars + maxCharsForArgs + spaceCharsForArgs + otherChars;
        static_assert(totalMaxChars < BUFFER_CAPACITY, "cannot write line larger than buffer capacity");

        if (totalMaxChars > availableSpace() && !flush()) {
            return false;
        }

        auto* buffer = out();
        const auto* start = buffer;

        *buffer = type;
        ++buffer;

        *buffer = ' ';
        ++buffer;

        buffer = writeHexNumbers(buffer, args...);

        *buffer = '\n';
        ++buffer;

        bufferSize += buffer - start;

        return true;
    }

    inline static unsigned clz(unsigned V)
    {
        return __builtin_clz(V);
    }

    inline static unsigned clz(long unsigned V)
    {
        return __builtin_clzl(V);
    }

    inline static unsigned clz(long long unsigned V)
    {
        return __builtin_clzll(V);
    }

    template <typename V>
    static char* writeHexNumber(char* buffer, V value)
    {
        static_assert(std::is_unsigned<V>::value, "can only convert unsigned numbers to hex");

        constexpr const unsigned numBits = sizeof(value) * 8;
        static_assert(numBits <= 64, "only up to 64bit of input are supported");

        // clz is undefined for 0, so handle that manually
        const unsigned zeroBits = value ? clz(value) : numBits;
        assert(zeroBits <= numBits);

        const unsigned usedBits = numBits - zeroBits;
        // 2^(usedBits) = 16^(requiredBufSize) = 2^(4 * requiredBufSize)
        // usedBits = 4 * requiredBufSize
        // requiredBufSize = usedBits / 4
        // but we need to round up, so actually use (usedBits + 3) / 4
        // and for 0 input, we still need one char, so use at least 1
        const unsigned requiredBufSize = std::max(1u, (usedBits + 3) / 4);
        assert(requiredBufSize <= 16);

        constexpr const char hexChars[16] = {'0', '1', '2', '3', '4', '5', '6', '7',
                                             '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

        char* out = buffer + requiredBufSize - 1;
        while (value >= 16) {
            const auto rest = value % 16;
            value /= 16;

            *out = hexChars[rest];
            --out;
        }
        *out = hexChars[value];
        assert(out == buffer);

        return buffer + requiredBufSize;
    }

    template <typename V>
    static char* writeHexNumbers(char* buffer, V value)
    {
        return writeHexNumber(buffer, value);
    }

    template <typename V, typename... T>
    static char* writeHexNumbers(char* buffer, V value, T... args)
    {
        buffer = writeHexNumber(buffer, value);
        *buffer = ' ';
        ++buffer;
        return writeHexNumbers(buffer, args...);
    }

    bool flush()
    {
        if (!canWrite()) {
            return false;
        } else if (!bufferSize) {
            return true;
        }

        int ret = 0;
        do {
            ret = ::write(fd, buffer.get(), bufferSize);
        } while (ret < 0 && errno == EINTR);

        if (ret < 0) {
            return false;
        }

        bufferSize = 0;

        return true;
    }

    bool canWrite() const
    {
        return fd != -1;
    }

    void close()
    {
        if (fd != -1) {
            ::close(fd);
            fd = -1;
        }
    }

private:
    size_t availableSpace() const
    {
        return BUFFER_CAPACITY - bufferSize;
    }

    char* out()
    {
        return buffer.get() + bufferSize;
    }

    int fd = -1;
    unsigned bufferSize = 0;
    std::unique_ptr<char[]> buffer;
};

#endif
