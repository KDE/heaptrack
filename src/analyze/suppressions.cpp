/*
    SPDX-FileCopyrightText: 2021 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include "suppressions.h"

#include <cstring>
#include <fstream>
#include <iostream>

#include <boost/algorithm/string/trim.hpp>

namespace {
std::vector<std::string> parseSuppressionsFile(std::istream& input)
{
    std::vector<std::string> ret;
    std::string line;
    while (std::getline(input, line)) {
        auto suppression = parseSuppression(line);
        if (!suppression.empty()) {
            ret.push_back(std::move(suppression));
        }
    }
    return ret;
}

/**
 * This function is based on the TemplateMatch function found in
 *     llvm-project/compiler-rt/lib/sanitizer_common/sanitizer_common.cpp
 * The code was licensed under Apache License v2.0 with LLVM Exceptions
 */
bool TemplateMatch(const char* templ, const char* str)
{
    if ((!str) || str[0] == 0)
        return false;
    bool start = false;
    if (templ && templ[0] == '^') {
        start = true;
        templ++;
    }
    bool asterisk = false;
    while (templ && templ[0]) {
        if (templ[0] == '*') {
            templ++;
            start = false;
            asterisk = true;
            continue;
        }
        if (templ[0] == '$')
            return str[0] == 0 || asterisk;
        if (str[0] == 0)
            return false;
        char* tpos = (char*)strchr(templ, '*');
        char* tpos1 = (char*)strchr(templ, '$');
        if ((!tpos) || (tpos1 && tpos1 < tpos))
            tpos = tpos1;
        if (tpos)
            tpos[0] = 0;
        const char* str0 = str;
        const char* spos = strstr(str, templ);
        str = spos + strlen(templ);
        templ = tpos;
        if (tpos)
            tpos[0] = tpos == tpos1 ? '$' : '*';
        if (!spos)
            return false;
        if (start && spos != str0)
            return false;
        start = false;
        asterisk = false;
    }
    return true;
}
}

std::string parseSuppression(std::string line)
{
    boost::trim(line, std::locale::classic());
    if (line.empty() || line[0] == '#') {
        // comment
        return {};
    } else if (line.compare(0, 5, "leak:") == 0) {
        return line.substr(5);
    }
    std::cerr << "invalid suppression line: " << line << '\n';
    return {};
}

std::vector<std::string> parseSuppressions(const std::string& suppressionFile, bool* ok)
{
    if (ok) {
        *ok = true;
    }

    if (suppressionFile.empty()) {
        return {};
    }

    auto stream = std::ifstream(suppressionFile);
    if (!stream.is_open()) {
        std::cerr << "failed to open suppression file: " << suppressionFile << '\n';
        if (ok) {
            *ok = false;
        }
        return {};
    }

    return parseSuppressionsFile(stream);
}

bool matchesSuppression(const std::string& suppression, const std::string& haystack)
{
    return suppression == haystack || TemplateMatch(suppression.c_str(), haystack.c_str());
}

std::vector<Suppression> builtinSuppressions()
{
    return {
        // libc
        {"__nss_module_allocate", 0, 0},
        {"__gconv_read_conf", 0, 0},
        {"__new_exitfn", 0, 0},
        {"tzset_internal", 0, 0},
        // dynamic linker
        {"dl_open_worker", 0, 0},
        // glib event loop
        {"g_main_context_new", 0, 0},
        {"g_main_context_new", 0, 0},
        {"g_thread_self", 0, 0},
    };
}
