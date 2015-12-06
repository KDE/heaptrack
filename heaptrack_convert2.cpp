/*
 * Copyright 2014 Milian Wolff <mail@milianw.de>
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

#include "sqlitewrapper.h"
#include "linereader.h"

#include <iostream>
#include <cstdio>
#include <stdio_ext.h>
#include <unordered_map>

using namespace std;

namespace {

sqlite::Database initSql(const string& file)
{
    remove(file.c_str());

    // improve performance of sqlite by removing temporary allocations
    sqlite3_config(SQLITE_CONFIG_LOOKASIDE, 1000, 500);

    auto db = sqlite::open(file);
    sqlite::execute(db, R"(
        CREATE TABLE Pointers (
            pointer UNSIGNED INTEGER PRIMARY KEY ASC,
            traceId UNSIGNED INTEGER,
            size UNSIGNED INTEGER
        ) WITHOUT ROWID
    )");

    // improve performance of bulk writes
    sqlite::execute(db, R"(
        PRAGMA synchronous = OFF
    )");
    return db;
}

void convertToSql(istream& in, sqlite::Database db)
{
    LineReader reader;

    sqlite::execute(db, R"(
        BEGIN TRANSACTION
    )");

    sqlite::Query findPointer(db, R"(
        SELECT traceId, size FROM Pointers WHERE pointer = ?1
    )");

    sqlite::Query pointers(db, R"(
        INSERT OR REPLACE INTO Pointers VALUES (?1, ?2, ?3)
    )");

    while (reader.getLine(in)) {
        if (reader.mode() == 's') {
        } else if (reader.mode() == 't') {
        } else if (reader.mode() == 'i') {
        } else if (reader.mode() == '+') {
            uint64_t size = 0;
            uint64_t traceId = 0;
            uint64_t ptr = 0;
            reader >> size;
            reader >> traceId;
            reader >> ptr;
            pointers.bindAll(1, ptr, traceId, size);
            pointers.execute();
            pointers.reset();
        } else if (reader.mode() == '-') {
            uint64_t ptr = 0;
            reader >> ptr;
            findPointer.bind(1, ptr);
            findPointer.execute();
//             cerr << "-\t" << ptr << " == " << findPointer.value(0) << " / " << findPointer.value(1) << "\n";
            findPointer.reset();
        } else if (reader.mode() == '#') {
            // comment or empty line
            continue;
        } else if (reader.mode() == 'c') {
        } else if (reader.mode() == 'X') {
        } else if (reader.mode() == 'A') {
        } else {
            cerr << "failed to parse line: " << reader.line() << endl;
        }
    }

    sqlite::execute(db, R"(
        END TRANSACTION
    )");

    cout << "finalizing...\n";
//     sqlite::execute(db, R"(
//         VACUUM
//     )");
    cout << "done\n";
}

}

int main(int argc, char** argv)
{
    if (argc != 2) {
        cerr << "heaptrack_convert OUTPUT_FILE < INPUT\n";
        return 1;
    }
    // optimize: we only have a single thread
    ios_base::sync_with_stdio(false);
    __fsetlocking(stdout, FSETLOCKING_BYCALLER);
    __fsetlocking(stdin, FSETLOCKING_BYCALLER);

    auto db = initSql(argv[1]);
    convertToSql(cin, db);

    return 0;
}
