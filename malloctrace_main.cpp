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
#include <sstream>
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
    void init()
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
                                            cerr << "Failed to initialize backtrace fileline for "
                                                 << (module->isExe ? "executable" : "library") << module->fileName
                                                 << ": " << msg << " (error code " << errnum << ")" << endl;
                                        }, this);
        }
    }

    string resolveAddress(uintptr_t offset) const
    {
        string ret;
        if (!backtraceState) {
            return ret;
        }
        backtrace_syminfo(backtraceState, baseAddress + offset,
                          [] (void *data, uintptr_t /*pc*/, const char *symname, uintptr_t /*symval*/, uintptr_t /*symsize*/) {
                            if (symname) {
                                *reinterpret_cast<string*>(data) = symname;
                            }
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
    /// TODO: vector?
    unordered_map<unsigned int, Module> modules;
};

}

int main(int argc, char** argv)
{
    if (argc < 2) {
        printUsage(cerr);
        return 1;
    }

    AccumulatedTraceData data;

    for (int i = 1; i < argc; ++i) {
        fstream in(argv[i], ios_base::in);
        if (!in.is_open()) {
            cerr << "Failed to open malloctrace log file: " << argv[1] << endl;
            cerr << endl;
            printUsage(cerr);
            return 1;
        }

        string line;
        line.reserve(1024);
        while (in.good()) {
            getline(in, line);
            stringstream lineIn(line, ios_base::in);
            char mode;
            lineIn >> mode;
            if (mode == 'm') {
                Module module;
                module.backtraceState = nullptr;
                unsigned int id;
                lineIn >> id;
                lineIn >> module.fileName;
                lineIn << hex;
                lineIn >> module.baseAddress;
                lineIn << dec;
                lineIn >> module.isExe;
                module.init();
                data.modules[id] = module;
            } else if (mode == '+') {
                size_t size = 0;
                lineIn >> size;
                lineIn << hex;
                void* ptr = nullptr;
                lineIn >> ptr;
                cout << "GOGOGO " << size << ' ' << ptr << '\n';
                while (lineIn.good()) {
                    unsigned int moduleId = 0;
                    lineIn >> moduleId;
                    if (!moduleId) {
                        break;
                    }
                    uintptr_t offset = 0;
                    lineIn << hex;
                    lineIn >> offset;
                    lineIn << dec;
                    auto module = data.modules[moduleId];
                    cout << moduleId << '\t' << offset << '\t' << module.resolveAddress(offset) << ' ' << module.fileName << '\n';
                }
            }
        }
    }

    return 0;
}
