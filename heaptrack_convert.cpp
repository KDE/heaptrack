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
        CREATE TABLE Traces (
            id UNSIGNED INTEGER PRIMARY KEY ASC,
            instructionPointer UNSIGNED INTEGER,
            parent UNSIGNED INTEGER
        ) WITHOUT ROWID
    )");
    sqlite::execute(db, R"(
        CREATE TABLE Strings (
            id UNSIGNED INTEGER PRIMARY KEY ASC,
            string TEXT
        ) WITHOUT ROWID
    )");
    sqlite::execute(db, R"(
        CREATE TABLE InstructionPointers (
            id UNSIGNED INTEGER PRIMARY KEY ASC,
            pointer UNSIGNED INTEGER,
            module UNSIGNED INTEGER,
            function UNSIGNED INTEGER,
            file UNSIGNED INTEGER,
            line UNSIGNED INTEGER
        ) WITHOUT ROWID
    )");
    sqlite::execute(db, R"(
        CREATE TABLE Allocations (
            id UNSIGNED INTEGER PRIMARY KEY ASC,
            size INTEGER,
            trace UNSIGNED INTEGER
        ) WITHOUT ROWID
    )");
    sqlite::execute(db, R"(
        CREATE TABLE Timestamps (
            id UNSIGNED INTEGER PRIMARY KEY ASC,
            time UNSIGNED INTEGER,
            allocations UNSIGNED INTEGER
        ) WITHOUT ROWID
    )");
    sqlite::execute(db, R"(
        CREATE TABLE Metadata (
            id UNSIGNED INTEGER PRIMARY KEY ASC,
            string TEXT,
            value TEXT
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
    unordered_map<uint64_t, uint64_t> ptrToAllocationId;
    ptrToAllocationId.reserve(1000000);
    LineReader reader;

    sqlite::execute(db, R"(
        BEGIN TRANSACTION
    )");

    sqlite::InsertQuery strings(db, R"(
        INSERT INTO Strings VALUES (?1, ?2)
    )");

    sqlite::InsertQuery traces(db, R"(
        INSERT INTO Traces VALUES (?1, ?2, ?3)
    )");

    sqlite::InsertQuery instructionPointers(db, R"(
        INSERT INTO InstructionPointers VALUES (?1, ?2, ?3, ?4, ?5, ?6)
    )");

    uint64_t allocationEntries = 0;
    sqlite::Query allocations(db, R"(
        INSERT INTO Allocations VALUES (?1, ?2, ?3)
    )");
    sqlite::Query deallocations(db, R"(
        INSERT INTO Allocations (id, size, trace)
        SELECT ?1, -b.size, b.trace FROM Allocations AS b WHERE b.id = ?2
    )");

    sqlite::InsertQuery timestamps(db, R"(
        INSERT INTO Timestamps VALUES (?1, ?2, ?3)
    )");

    sqlite::InsertQuery metadata(db, R"(
        INSERT INTO Metadata VALUES (?1, ?2, ?3)
    )");

    while (reader.getLine(in)) {
        if (reader.mode() == 's') {
            strings.insert(reader.line().c_str() + 2);
        } else if (reader.mode() == 't') {
            uint64_t ipIndex = 0;
            uint64_t parentIndex = 0;
            reader >> ipIndex;
            reader >> parentIndex;
            traces.insert(ipIndex, parentIndex);
        } else if (reader.mode() == 'i') {
            uint64_t instructionPointer = 0;
            uint64_t moduleIndex = 0;
            uint64_t functionIndex = 0;
            uint64_t fileIndex = 0;
            int line = 0;
            reader >> instructionPointer;
            reader >> moduleIndex;
            reader >> functionIndex;
            reader >> fileIndex;
            reader >> line;
            instructionPointers.insert(instructionPointer, moduleIndex, functionIndex, fileIndex, line);
        } else if (reader.mode() == '+') {
            uint64_t size = 0;
            uint64_t traceId = 0;
            uint64_t ptr = 0;
            reader >> size;
            reader >> traceId;
            reader >> ptr;
            allocations.bindAll(1, allocationEntries, size, traceId);
            allocations.execute();
            allocations.reset();
            ptrToAllocationId[ptr] = allocationEntries;
            ++allocationEntries;
        } else if (reader.mode() == '-') {
            uint64_t ptr = 0;
            reader >> ptr;
            auto it = ptrToAllocationId.find(ptr);
            if (it != ptrToAllocationId.end()) {
                auto allocationId = it->second;
                ptrToAllocationId.erase(it);
                deallocations.bindAll(1, allocationEntries, allocationId);
                deallocations.execute();
                deallocations.reset();
                ++allocationEntries;
            } else {
                cerr << "unknown ptr passed to free: " << reader.line() << '\n';
            }
        } else if (reader.mode() == '#') {
            // comment or empty line
            continue;
        } else if (reader.mode() == 'c') {
            uint64_t timestamp = 0;
            reader >> timestamp;
            timestamps.insert(timestamp, allocationEntries);
        } else if (reader.mode() == 'X') {
            const auto debuggee = reader.line().c_str() + 2;
            metadata.insert("debuggee", debuggee);
        } else if (reader.mode() == 'A') {
            metadata.insert("attached", "true");
        } else {
            cerr << "failed to parse line: " << reader.line() << endl;
        }
    }

    sqlite::execute(db, R"(
        END TRANSACTION
    )");

    cout << "finalizing...\n";
    sqlite::execute(db, R"(
        VACUUM
    )");
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