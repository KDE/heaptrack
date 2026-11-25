/*
    SPDX-FileCopyrightText: 2018 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
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
#include <cstring>
#include <string>

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
        for (int remainingTries = 2; remainingTries > 0; --remainingTries) {
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
            if (static_cast<size_t>(ret) + 1 > BUFFER_CAPACITY) {
                // the message doesn't fit into the buffer capacity at all.
                // This can happen e.g. when the interpreted module path or
                // function string is excessively long, see
                // https://bugs.kde.org/show_bug.cgi?id=425838 . Buffering it
                // is impossible, and asserting (debug builds) or silently
                // dropping the data (release builds) is much worse than
                // writing it out directly. Flush the buffer first to keep
                // the output in order, then write the complete message.
                if (!flush()) {
                    return false;
                }
                std::unique_ptr<char[]> msg(new char[ret + 1]);
                ret = snprintf(msg.get(), ret + 1, fmt, args...);
                if (ret < 0) {
                    return false;
                }
                if (!writeAll(msg.get(), ret)) {
                    return false;
                }
                return true;
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

        {
            // first write the size of the string
            constexpr const int maxHexCharsForSize = 16 + 1; // 2^64 == 16^16 + 1 space
            if (availableSpace() < maxHexCharsForSize && !flush())
                return false;

            const auto start = out();
            auto end = writeHexNumber(start, length);
            *end = ' ';
            ++end;
            bufferSize += end - start;
        }

        if (availableSpace() < length) {
            if (!flush()) {
                return false;
            }
            if (availableSpace() < length) {
                // ::write may return after having written only part of the
                // data, e.g. when interrupted by a signal, so keep writing
                // until everything was sent
                return writeAll(line.data(), length);
            }
        }
        memcpy(out(), line.data(), length);
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

        if (!writeAll(buffer.get(), bufferSize)) {
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
    /**
     * write the complete data to the file descriptor, retrying on
     * interruptions and handling partial writes
     */
    bool writeAll(const char* data, size_t length)
    {
        while (length > 0) {
            ssize_t ret = ::write(fd, data, length);
            if (ret < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return false;
            } else if (ret == 0) {
                return false;
            }
            data += ret;
            length -= ret;
        }
        return true;
    }

    size_t availableSpace() const
    {
        return BUFFER_CAPACITY - bufferSize;
    }

    char* out()
    {
        return buffer.get() + bufferSize;
    }

    int fd = -1;
    size_t bufferSize = 0;
    std::unique_ptr<char[]> buffer;
};

#endif
