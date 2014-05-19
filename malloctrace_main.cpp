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

#include <iostream>
#include <fstream>
#include <unordered_map>

#include "libbacktrace/backtrace.h"

using namespace std;

namespace {

void printUsage(ostream& out)
{
    out << "malloctrace_main MALLOCTRACE_LOG_FILE..." << endl;
}

struct Module
{
    Module(string _fileName, uintptr_t baseAddress, bool isExe)
        : backtraceState(nullptr)
        , fileName(move(_fileName))
        , baseAddress(baseAddress)
        , isExe(isExe)
    {
        backtraceState = backtrace_create_state(fileName.c_str(), /* we are single threaded, so: not thread safe */ false,
                                                [] (void *data, const char *msg, int errnum) {
                                                    const Module* module = reinterpret_cast<Module*>(data);
                                                    cerr << "Failed to create backtrace state for file " << module->fileName
                                                         << ": " << msg << " (error code " << errnum << ")" << endl;
                                                }, this);

        if (backtraceState) {
            backtrace_fileline_initialize(backtraceState, baseAddress, isExe,
                                          [] (void *data, const char *msg, int errnum) {
                                            const Module* module = reinterpret_cast<Module*>(data);
                                            cerr << "Failed to initialize backtrace fileline for file " << module->fileName
                                                 << ", base address: " << hex << module->baseAddress << dec << ", exe: " << module->isExe
                                                 << ": " << msg << " (error code " << errnum << ")" << endl;
                                        }, this);
        }
    }

    string resolveAddress(uintptr_t address)
    {
        string ret;
        backtrace_syminfo(backtraceState, address,
                          [] (void *data, uintptr_t /*pc*/, const char *symname, uintptr_t /*symval*/, uintptr_t /*symsize*/) {
                            *reinterpret_cast<string*>(data) = symname;
                          }, &errorCallback, &ret);
        return ret;
    }

    static void errorCallback(void */*data*/, const char *msg, int errnum)
    {
        cerr << "Module backtrace error (code " << errnum << "): " << msg << endl;
    }

    backtrace_state* backtraceState;
    string fileName;
    uintptr_t baseAddress;
    bool isExe;
};

struct AccumulatedTraceData
{
    unordered_map<unsigned int, Module> modules;
};

}

int main(int argc, char** argv)
{
    if (argc < 2) {
        printUsage(cerr);
        return 1;
    }


    for (int i = 1; i < argc; ++i) {
        fstream in(argv[i], std::fstream::in);
        if (!in.is_open()) {
            cerr << "Failed to open malloctrace log file: " << argv[1] << endl;
            cerr << endl;
            printUsage(cerr);
            return 1;
        }
        // TODO: parse file
    }

    return 0;
}
