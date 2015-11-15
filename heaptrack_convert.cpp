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
        )
    )");
    sqlite::execute(db, R"(
        CREATE TABLE Strings (
            id UNSIGNED INTEGER PRIMARY KEY ASC,
            string TEXT
        )
    )");
    sqlite::execute(db, R"(
        CREATE TABLE InstructionPointers (
            id UNSIGNED INTEGER PRIMARY KEY ASC,
            pointer UNSIGNED INTEGER,
            module UNSIGNED INTEGER,
            function UNSIGNED INTEGER,
            file UNSIGNED INTEGER,
            line UNSIGNED INTEGER
        )
    )");
    sqlite::execute(db, R"(
        CREATE TABLE Allocations (
            id UNSIGNED INTEGER PRIMARY KEY ASC,
            size UNSIGNED INTEGER,
            trace UNSIGNED INTEGER,
            pointer UNSIGNED INTEGER
        )
    )");
    sqlite::execute(db, R"(
        CREATE TABLE Deallocations (
            id UNSIGNED INTEGER PRIMARY KEY ASC,
            pointer UNSIGNED INTEGER
        )
    )");
    sqlite::execute(db, R"(
        CREATE TABLE Timestamps (
            id UNSIGNED INTEGER PRIMARY KEY ASC,
            time UNSIGNED INTEGER
            allocationId UNSIGNED INTEGER,
            deallocationId UNSIGNED INTEGER,
        )
    )");
    sqlite::execute(db, R"(
        CREATE TABLE Metadata (
            id UNSIGNED INTEGER PRIMARY KEY ASC,
            string TEXT,
            value TEXT
        )
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

    sqlite::InsertQuery strings(db, R"(
        INSERT INTO Strings VALUES (?1, ?2)
    )");

    sqlite::InsertQuery traces(db, R"(
        INSERT INTO Traces VALUES (?1, ?2, ?3)
    )");

    sqlite::InsertQuery instructionPointers(db, R"(
        INSERT INTO InstructionPointers VALUES (?1, ?2, ?3, ?4, ?5, ?6)
    )");

    sqlite::InsertQuery allocations(db, R"(
        INSERT INTO Allocations VALUES (?1, ?2, ?3, ?4)
    )");

    sqlite::InsertQuery deallocations(db, R"(
        INSERT INTO Deallocations VALUES (?1, ?2)
    )");

    sqlite::InsertQuery timestamps(db, R"(
        INSERT INTO Timestamps VALUES (?1, ?2, ?3, ?4)
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
            allocations.insert(size, traceId, ptr);
        } else if (reader.mode() == '-') {
            uint64_t ptr = 0;
            reader >> ptr;
            deallocations.insert(ptr);
        } else if (reader.mode() == '#') {
            // comment or empty line
            continue;
        } else if (reader.mode() == 'c') {
            uint64_t timestamp = 0;
            reader >> timestamp;
            timestamps.insert(timestamp, allocations.rowsInserted(), deallocations.rowsInserted());
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