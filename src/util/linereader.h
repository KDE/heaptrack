/*
    SPDX-FileCopyrightText: 2014-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#ifndef LINEREADER_H
#define LINEREADER_H

#include <istream>
#include <string>

/**
 * Optimized class to speed up reading of the potentially big data files.
 *
 * sscanf or istream are just slow when reading plain hex numbers. The
 * below does all we need and thus far less than what the generic functions
 * are capable of. We are not locale aware e.g.
 */
class LineReader
{
public:
    LineReader()
    {
        m_line.reserve(1024);
    }

    bool getLine(std::istream& in)
    {
        if (!in.good()) {
            return false;
        }
        std::getline(in, m_line);
        m_it = m_line.cbegin();
        if (m_line.length() > 2) {
            m_it += 2;
        } else {
            m_it = m_line.cend();
        }
        return true;
    }

    char mode() const
    {
        return m_line.empty() ? '#' : m_line[0];
    }

    const std::string& line() const
    {
        return m_line;
    }

    template <typename T>
    bool readHex(T& in)
    {
        auto it = m_it;
        const auto end = m_line.cend();
        if (it == end) {
            return false;
        }

        T hex = 0;
        do {
            const char c = *it;
            if ('0' <= c && c <= '9') {
                hex *= 16;
                hex += c - '0';
            } else if ('a' <= c && c <= 'f') {
                hex *= 16;
                hex += c - 'a' + 10;
            } else if (c == ' ') {
                ++it;
                break;
            } else {
                fprintf(stderr, "unexpected non-hex char: %d %zx\n", c, std::distance(m_line.cbegin(), it));
                return false;
            }
            ++it;
        } while (it != end);

        in = hex;
        m_it = it;
        return true;
    }

    bool operator>>(int64_t& hex)
    {
        return readHex(hex);
    }

    bool operator>>(uint64_t& hex)
    {
        return readHex(hex);
    }

    bool operator>>(uint32_t& hex)
    {
        return readHex(hex);
    }

    bool operator>>(int& hex)
    {
        return readHex(hex);
    }

    void setExpectedSizedStrings(bool expectSizedStrings)
    {
        m_expectSizedStrings = expectSizedStrings;
    }

    bool operator>>(std::string& str)
    {
        if (m_expectSizedStrings) {
            uint64_t size = 0;
            if (!(*this >> size) || size > static_cast<uint64_t>(std::distance(m_it, m_line.cend()))) {
                return false;
            }
            auto start = m_it;
            m_it += size;
            str.assign(start, m_it);
            if (m_it != m_line.cend()) {
                // eat trailing whitespace
                ++m_it;
            }
            return true;
        }

        auto it = m_it;
        const auto end = m_line.cend();
        while (it != end && *it != ' ') {
            ++it;
        }
        if (it != m_it) {
            str = std::string(m_it, it);
            if (*it == ' ') {
                ++it;
            }
            m_it = it;
            return true;
        } else {
            return false;
        }
    }

    bool operator>>(bool& flag)
    {
        if (m_it != m_line.cend()) {
            flag = *m_it;
            m_it++;
            if (*m_it == ' ') {
                ++m_it;
            }
            return true;
        } else {
            return false;
        }
    }

private:
    bool m_expectSizedStrings = false;
    std::string m_line;
    std::string::const_iterator m_it;
};

#endif // LINEREADER_H
