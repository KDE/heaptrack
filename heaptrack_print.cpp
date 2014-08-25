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

/**
 * @file heaptrack_print.cpp
 *
 * @brief Evaluate and print the collected heaptrack data.
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <map>
#include <vector>
#include <memory>
#include <tuple>
#include <algorithm>

#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/program_options.hpp>

using namespace std;
namespace po = boost::program_options;

namespace {

struct AddressInformation
{
    string function;
    string file;
    int line = 0;
};

ostream& operator<<(ostream& out, const AddressInformation& info)
{
    out << info.function;
    if (!info.file.empty()) {
        out << " in " << info.file << ':' << info.line;
    }
    return out;
}

struct InstructionPointer
{
    uintptr_t instructionPointer = 0;
    size_t parentIndex = 0;
    size_t moduleIndex = 0;
    size_t functionIndex = 0;
    size_t fileIndex = 0;
    int line = 0;
};

struct Allocation
{
    // backtrace entry point
    size_t ipIndex;
    // number of allocations
    size_t allocations;
    // bytes allocated in total
    size_t allocated;
    // amount of bytes leaked
    size_t leaked;
    // largest amount of bytes allocated
    size_t peak;
};

/**
 * Information for a single call to an allocation function
 */
struct AllocationInfo
{
    size_t ipIndex;
    size_t size;
};

struct AccumulatedTraceData
{
    AccumulatedTraceData()
    {
        instructionPointers.reserve(65536);
        strings.reserve(16384);
        allocations.reserve(16384);
        activeAllocations.reserve(65536);
    }

    void clear()
    {
        mainIndex = 0;
        instructionPointers.clear();
        strings.clear();
        allocations.clear();
        activeAllocations.clear();
    }

    void printBacktrace(const size_t ipIndex, ostream& out) const
    {
        printBacktrace(findIp(ipIndex), out);
    }

    void printBacktrace(InstructionPointer ip, ostream& out) const
    {
        while (ip.instructionPointer) {
            out << "0x" << hex << ip.instructionPointer << dec;
            if (ip.moduleIndex) {
                out << ' ' << stringify(ip.moduleIndex);
            } else {
                out << " <unknown module>";
            }
            if (ip.functionIndex) {
                out << ' ' << prettyFunction(stringify(ip.functionIndex));
            } else {
                out << " ??";
            }
            if (ip.fileIndex) {
                out << ' ' << stringify(ip.fileIndex) << ':' << ip.line;
            }
            out << '\n';

            if (mainIndex && ip.functionIndex == mainIndex) {
                break;
            }

            ip = findIp(ip.parentIndex);
        };
    }

    Allocation& findAllocation(const size_t ipIndex)
    {
        auto it = lower_bound(allocations.begin(), allocations.end(), ipIndex,
                                [] (const Allocation& allocation, const size_t ipIndex) -> bool {
                                    return allocation.ipIndex < ipIndex;
                                });
        if (it == allocations.end() || it->ipIndex != ipIndex) {
            it = allocations.insert(it, {ipIndex, 0, 0, 0, 0});
        }
        return *it;
    }

    const string& stringify(const size_t stringId) const
    {
        if (!stringId || stringId > strings.size()) {
            static const string empty;
            return empty;
        } else {
            return strings.at(stringId - 1);
        }
    }

    string prettyFunction(const string& function) const
    {
        if (!shortenTemplates) {
            return function;
        }
        string ret;
        ret.reserve(function.size());
        int depth = 0;
        for (size_t i = 0; i < function.size(); ++i) {
            const auto c = function[i];
            if (c == '<') {
                ++depth;
                if (depth == 1) {
                    ret.push_back(c);
                }
            } else if (c == '>') {
                --depth;
            }
            if (depth) {
                continue;
            }
            ret.push_back(c);
        }
        return ret;
    }

    bool read(istream& in)
    {
        clear();

        string line;
        line.reserve(1024);

        stringstream lineIn(ios_base::in);
        lineIn << hex;

        while (in.good()) {
            getline(in, line);
            if (line.empty()) {
                continue;
            }
            const char mode = line[0];
            lineIn.str(line);
            lineIn.clear();
            // skip mode and leading whitespace
            lineIn.seekg(2);
            if (mode == 's') {
                strings.push_back(line.substr(2));
            } else if (mode == 'i') {
                InstructionPointer ip;
                lineIn >> ip.instructionPointer;
                lineIn >> ip.parentIndex;
                lineIn >> ip.moduleIndex;
                lineIn >> ip.functionIndex;
                lineIn >> ip.fileIndex;
                lineIn >> ip.line;
                instructionPointers.push_back(ip);
            } else if (mode == '+') {
                size_t size = 0;
                lineIn >> size;
                size_t ipId = 0;
                lineIn >> ipId;
                uintptr_t ptr = 0;
                lineIn >> ptr;
                if (lineIn.bad()) {
                    cerr << "failed to parse line: " << line << endl;
                    return false;
                }

                activeAllocations[ptr] = {ipId, size};

                auto& allocation = findAllocation(ipId);
                allocation.leaked += size;
                allocation.allocated += size;
                ++allocation.allocations;
                if (allocation.leaked > allocation.peak) {
                    allocation.peak = allocation.leaked;
                }
                totalAllocated += size;
                ++totalAllocations;
                leaked += size;
                if (leaked > peak) {
                    peak = leaked;
                }
                ++sizeHistogram[size];
            } else if (mode == '-') {
                uintptr_t ptr = 0;
                lineIn >> ptr;
                if (lineIn.bad()) {
                    cerr << "failed to parse line: " << line << endl;
                    return false;
                }
                auto ip = activeAllocations.find(ptr);
                if (ip == activeAllocations.end()) {
                    cerr << "unknown pointer in line: " << line << endl;
                    continue;
                }
                const auto info = ip->second;
                activeAllocations.erase(ip);

                auto& allocation = findAllocation(info.ipIndex);
                if (!allocation.allocations || allocation.leaked < info.size) {
                    cerr << "inconsistent allocation info, underflowed allocations of " << info.ipIndex << endl;
                    allocation.leaked = 0;
                    allocation.allocations = 0;
                } else {
                    allocation.leaked -= info.size;
                }
                leaked -= info.size;
            } else {
                cerr << "failed to parse line: " << line << endl;
            }
        }

        // find index of "main" index which can be used to terminate backtraces
        // and prevent printing stuff above main, which is usually uninteresting
        auto it = find(strings.begin(), strings.end(), "main");
        if (it != strings.end()) {
            mainIndex = distance(strings.begin(), it);
        }

        /// these are leaks, but we have the same data in \c allocations as well
        activeAllocations.clear();
        return true;
    }

    bool shortenTemplates = false;

    vector<Allocation> allocations;
    map<size_t, size_t> sizeHistogram;
    size_t totalAllocated = 0;
    size_t totalAllocations = 0;
    size_t peak = 0;
    size_t leaked = 0;

private:
    InstructionPointer findIp(const size_t ipIndex) const
    {
        if (!ipIndex || ipIndex > instructionPointers.size()) {
            return {};
        } else {
            return instructionPointers[ipIndex - 1];
        }
    }

    size_t mainIndex = 0;
    unordered_map<uintptr_t, AllocationInfo> activeAllocations;
    vector<InstructionPointer> instructionPointers;
    vector<string> strings;
};

}

int main(int argc, char** argv)
{
    po::options_description desc("Options");
    desc.add_options()
        ("file,f", po::value<string>()->required(), "The heaptrack data file to print.")
        ("shorten-templates,t", po::value<bool>()->default_value(true), "Shorten template identifiers.")
        ("print-peaks,p", po::value<bool>()->default_value(true), "Print backtraces to top allocators, sorted by peak consumption.")
        ("print-allocators,a", po::value<bool>()->default_value(true), "Print backtraces to top allocators, sorted by number of calls to allocation functions.")
        ("print-leaks,l", po::value<bool>()->default_value(false), "Print backtraces to leaked memory allocations.")
        ("print-histogram,H", po::value<bool>()->default_value(false), "Print allocation size histogram.")
        ("print-overall-allocated,o", po::value<bool>()->default_value(false), "Print top overall allocators, ignoring memory frees.")
        ("help,h", "Show this help message.");
    po::positional_options_description p;
    p.add("file", -1);

    po::variables_map vm;
    try {
        po::store(po::command_line_parser(argc, argv)
                    .options(desc).positional(p).run(), vm);
        if (vm.count("help")) {
            cout << "heaptrack_print - analyze heaptrack data files.\n"
                << "\n"
                << "heaptrack is a heap memory profiler which records information\n"
                << "about calls to heap allocation functions such as malloc, operator new etc. pp.\n"
                << "This print utility can then be used to analyze the generated data files.\n\n"
                << desc << endl;
            return 1;
        }
        po::notify(vm);
    } catch (const po::error& error) {
        cerr << "ERROR: " << error.what() << endl
             << endl << desc << endl;
        return 1;
    }

    AccumulatedTraceData data;

    // optimize: we only have a single thread
    ios_base::sync_with_stdio(false);

    const auto inputFile = vm["file"].as<string>();
    data.shortenTemplates = vm["shorten-templates"].as<bool>();
    const bool printHistogram = vm["print-histogram"].as<bool>();
    const bool printLeaks = vm["print-leaks"].as<bool>();
    const bool printOverallAlloc = vm["print-overall-allocated"].as<bool>();
    const bool printPeaks = vm["print-peaks"].as<bool>();
    const bool printAllocs = vm["print-allocators"].as<bool>();

    string fileName(inputFile);
    const bool isCompressed = boost::algorithm::ends_with(fileName, ".gz");
    ifstream file(fileName, isCompressed ? ios_base::in | ios_base::binary : ios_base::in);

    if (!file.is_open()) {
        cerr << "Failed to open heaptrack log file: " << inputFile << endl
             << endl << desc << endl;
        return 1;
    }

    boost::iostreams::filtering_istream in;
    if (isCompressed) {
        in.push(boost::iostreams::gzip_decompressor());
    }
    in.push(file);

    cout << "reading file \"" << fileName << "\" - please wait, this might take some time...";
    cout.flush();

    if (!data.read(in)) {
        return 1;
    }

    cout << "finished reading file, now analyzing data:\n" << endl;

    if (printAllocs) {
        // sort by amount of allocations
        sort(data.allocations.begin(), data.allocations.end(), [] (const Allocation& l, const Allocation &r) {
            return l.allocations > r.allocations;
        });
        cout << "MOST CALLS TO ALLOCATION FUNCTIONS\n";
        for (size_t i = 0; i < min(10lu, data.allocations.size()); ++i) {
            const auto& allocation = data.allocations[i];
            cout << allocation.allocations << " allocations at:\n";
            data.printBacktrace(allocation.ipIndex, cout);
            cout << '\n';
        }
        cout << endl;
    }

    if (printOverallAlloc) {
        // sort by amount of bytes allocated
        sort(data.allocations.begin(), data.allocations.end(), [] (const Allocation& l, const Allocation &r) {
            return l.allocated > r.allocated;
        });
        cout << "MOST BYTES ALLOCATED OVER TIME (ignoring deallocations)\n";
        for (size_t i = 0; i < min(10lu, data.allocations.size()); ++i) {
            const auto& allocation = data.allocations[i];
            cout << allocation.allocated << " bytes allocated at:\n";
            data.printBacktrace(allocation.ipIndex, cout);
            cout << '\n';
        }
        cout << endl;
    }

    if (printPeaks) {
        // sort by peak memory consumption
        sort(data.allocations.begin(), data.allocations.end(), [] (const Allocation& l, const Allocation &r) {
            return l.peak > r.peak;
        });
        cout << "PEAK MEMORY CONSUMERS\n";
        for (size_t i = 0; i < min(10lu, data.allocations.size()); ++i) {
            const auto& allocation = data.allocations[i];
            cout << allocation.peak << " bytes allocated at peak:\n";
            data.printBacktrace(allocation.ipIndex, cout);
            cout << '\n';
        }
        cout << endl;
    }

    if (printLeaks) {
        // sort by amount of leaks
        sort(data.allocations.begin(), data.allocations.end(), [] (const Allocation& l, const Allocation &r) {
            return l.leaked < r.leaked;
        });

        size_t totalLeakAllocations = 0;
        cout << "MEMORY LEAKS\n";
        for (const auto& allocation : data.allocations) {
            if (!allocation.leaked) {
                continue;
            }
            totalLeakAllocations += allocation.allocations;

            cout << allocation.leaked << " bytes leaked in " << allocation.allocations << " allocations at:\n";
            data.printBacktrace(allocation.ipIndex, cout);
            cout << '\n';
        }
        cout << data.leaked << " bytes leaked in total from " << totalLeakAllocations << " allocations" << endl;
    }

    cout << data.totalAllocated << " bytes allocated in total over " << data.totalAllocations
         << " allocations, peak consumption: " << data.peak << " bytes\n\n";

    if (printHistogram) {
        cout << "size histogram: " << endl;
        for (auto entry : data.sizeHistogram) {
            cout << entry.first << "\t" << entry.second << endl;
        }
    }

    return 0;
}
