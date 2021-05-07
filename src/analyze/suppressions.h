/*
 * Copyright 2021 Milian Wolff <mail@milianw.de>
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

#ifndef SUPPRESSIONS_H
#define SUPPRESSIONS_H

#include <string>
#include <vector>

std::string parseSuppression(std::string line);
std::vector<std::string> parseSuppressions(const std::string& suppressionFile, bool* ok);
bool matchesSuppression(const std::string& suppression, const std::string& haystack);

struct Suppression
{
    std::string pattern;
    int64_t matches = 0;
    int64_t leaked = 0;
};

#endif // SUPPRESSIONS_H
