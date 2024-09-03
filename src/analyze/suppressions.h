/*
    SPDX-FileCopyrightText: 2021 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#ifndef SUPPRESSIONS_H
#define SUPPRESSIONS_H

#include <string>
#include <vector>

#include <sys/types.h>

std::string parseSuppression(std::string line);
std::vector<std::string> parseSuppressions(const std::string& suppressionFile, bool* ok);
bool matchesSuppression(const std::string& suppression, const std::string& haystack);

struct Suppression
{
    std::string pattern;
    int64_t matches = 0;
    int64_t leaked = 0;
};

std::vector<Suppression> builtinSuppressions();

#endif // SUPPRESSIONS_H
