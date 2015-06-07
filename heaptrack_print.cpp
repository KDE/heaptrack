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

#include <boost/program_options.hpp>

#include "accumulatedtracedata.h"

#include <iostream>

#include "config.h"

using namespace std;
namespace po = boost::program_options;

struct Printer final : public AccumulatedTraceData
{
    void printIp(const IpIndex ip, ostream &out, const size_t indent = 0) const
    {
        printIp(findIp(ip), out, indent);
    }

    void printIndent(ostream& out, size_t indent, const char* indentString = "  ") const
    {
        while (indent--) {
            out << indentString;
        }
    }

    void printIp(const InstructionPointer& ip, ostream& out, const size_t indent = 0) const
    {
        printIndent(out, indent);

        if (ip.functionIndex) {
            out << prettyFunction(stringify(ip.functionIndex));
        } else {
            out << "0x" << hex << ip.instructionPointer << dec;
        }

        out << '\n';
        printIndent(out, indent + 1);

        if (ip.fileIndex) {
            out << "at " << stringify(ip.fileIndex) << ':' << ip.line << '\n';
            printIndent(out, indent + 1);
        }

        if (ip.moduleIndex) {
            out << "in " << stringify(ip.moduleIndex);
        } else {
            out << "in ??";
        }
        out << '\n';
    }

    void printBacktrace(const TraceIndex traceIndex, ostream& out,
                        const size_t indent = 0, bool skipFirst = false) const
    {
        if (!traceIndex) {
            out << "  ??";
            return;
        }
        printBacktrace(findTrace(traceIndex), out, indent, skipFirst);
    }

    void printBacktrace(TraceNode node, ostream& out, const size_t indent = 0,
                        bool skipFirst = false) const
    {
        while (node.ipIndex) {
            const auto& ip = findIp(node.ipIndex);
            if (!skipFirst) {
                printIp(ip, out, indent);
            }
            skipFirst = false;

            if (isStopIndex(ip.functionIndex)) {
                break;
            }

            node = findTrace(node.parentIndex);
        };
    }

    template<typename T, typename LabelPrinter, typename SubLabelPrinter>
    void printAllocations(T AllocationData::* member, LabelPrinter label, SubLabelPrinter sublabel)
    {
        if (mergeBacktraces) {
            printMerged(member, label, sublabel);
        } else {
            printUnmerged(member, label);
        }
    }

    template<typename T, typename LabelPrinter, typename SubLabelPrinter>
    void printMerged(T AllocationData::* member, LabelPrinter label, SubLabelPrinter sublabel)
    {
        auto sortOrder = [member] (const AllocationData& l, const AllocationData& r) {
            return l.*member > r.*member;
        };
        sort(mergedAllocations.begin(), mergedAllocations.end(), sortOrder);
        for (size_t i = 0; i < min(size_t(10), mergedAllocations.size()); ++i) {
            auto& allocation = mergedAllocations[i];
            if (!(allocation.*member)) {
                break;
            }
            label(allocation);
            printIp(allocation.ipIndex, cout);

            sort(allocation.traces.begin(), allocation.traces.end(), sortOrder);
            size_t handled = 0;
            const size_t subTracesToPrint = 5;
            for (size_t j = 0; j < min(subTracesToPrint, allocation.traces.size()); ++j) {
                const auto& trace = allocation.traces[j];
                sublabel(trace);
                handled += trace.*member;
                printBacktrace(trace.traceIndex, cout, 2, true);
            }
            if (allocation.traces.size() > subTracesToPrint) {
                cout << "  and ";
                if (member == &AllocationData::allocations) {
                    cout << (allocation.*member - handled);
                } else {
                    cout << formatBytes(allocation.*member - handled);
                }
                cout << " from " << (allocation.traces.size() - subTracesToPrint) << " other places\n";
            }
            cout << '\n';
        }
    }

    template<typename T, typename LabelPrinter>
    void printUnmerged(T AllocationData::* member, LabelPrinter label)
    {
        sort(allocations.begin(), allocations.end(),
            [member] (const Allocation& l, const Allocation &r) {
                return l.*member > r.*member;
            });
        for (size_t i = 0; i < min(size_t(10), allocations.size()); ++i) {
            const auto& allocation = allocations[i];
            if (!(allocation.*member)) {
                break;
            }
            label(allocation);
            printBacktrace(allocation.traceIndex, cout, 1);
            cout << '\n';
        }
        cout << endl;
    }

    void writeMassifHeader(const char* command)
    {
        // write massif header
        massifOut << "desc: heaptrack\n"
                  << "cmd: " << command << '\n'
                  << "time_unit: s\n";
    }

    void writeMassifSnapshot(size_t timeStamp, bool isLast)
    {
        if (!lastMassifPeak) {
            lastMassifPeak = leaked;
            massifAllocations = allocations;
        }
        massifOut
            << "#-----------\n"
            << "snapshot=" << massifSnapshotId << '\n'
            << "#-----------\n"
            << "time=" << (0.001 * timeStamp) << '\n'
            << "mem_heap_B=" << lastMassifPeak << '\n'
            << "mem_heap_extra_B=0\n"
            << "mem_stacks_B=0\n";

        if (massifDetailedFreq && (isLast || !(massifSnapshotId % massifDetailedFreq))) {
            massifOut << "heap_tree=detailed\n";
            const size_t threshold = double(lastMassifPeak) * massifThreshold * 0.01;
            writeMassifBacktrace(massifAllocations, lastMassifPeak, threshold, IpIndex());
        } else {
            massifOut << "heap_tree=empty\n";
        }

        ++massifSnapshotId;
        lastMassifPeak = 0;
    }

    void writeMassifBacktrace(const vector<Allocation>& allocations, size_t heapSize, size_t threshold,
                              const IpIndex& location, size_t depth = 0)
    {
        size_t skippedLeaked = 0;
        size_t numAllocs = 0;
        size_t skipped = 0;
        auto mergedAllocations = mergeAllocations(allocations);
        sort(mergedAllocations.begin(), mergedAllocations.end(), [] (const MergedAllocation& l, const MergedAllocation& r) {
            return l.leaked > r.leaked;
        });

        const auto ip = findIp(location);

        // skip anything below main
        const bool shouldStop = isStopIndex(ip.functionIndex);
        if (!shouldStop) {
            for (auto& merged : mergedAllocations) {
                if (!merged.leaked) {
                    // list is sorted, so we can bail out now - these entries are uninteresting for massif
                    break;
                }

                // skip items below threshold
                if (merged.leaked >= threshold) {
                    ++numAllocs;
                    // skip the first level of the backtrace, otherwise we'd endlessly recurse
                    for (auto& alloc : merged.traces) {
                        alloc.traceIndex = findTrace(alloc.traceIndex).parentIndex;
                    }
                } else {
                    ++skipped;
                    skippedLeaked += merged.leaked;
                }
            }
        }

        printIndent(massifOut, depth, " ");
        massifOut << 'n' << (numAllocs + (skipped ? 1 : 0)) << ": " << heapSize;
        if (!depth) {
            massifOut << " (heap allocation functions) malloc/new/new[], --alloc-fns, etc.\n";
        } else {
            massifOut << " 0x" << hex << ip.instructionPointer << dec
                      << ": ";
            if (ip.functionIndex) {
                massifOut << stringify(ip.functionIndex);
            } else {
                massifOut << "???";
            }

            massifOut << " (";
            if (ip.fileIndex) {
                massifOut << stringify(ip.fileIndex) << ':' << ip.line;
            } else if (ip.moduleIndex) {
                massifOut << stringify(ip.moduleIndex);
            } else {
                massifOut << "???";
            }
            massifOut << ")\n";
        }

        auto writeSkipped = [&] {
            if (skipped) {
                printIndent(massifOut, depth, " ");
                massifOut << " n0: " << skippedLeaked << " in " << skipped
                        << " places, all below massif's threshold (" << massifThreshold << ")\n";
                skipped = 0;
            }
        };

        if (!shouldStop) {
            for (const auto& merged : mergedAllocations) {
                if (merged.leaked && merged.leaked >= threshold) {
                    if (skippedLeaked > merged.leaked) {
                        // manually inject this entry to keep the output sorted
                        writeSkipped();
                    }
                    writeMassifBacktrace(merged.traces, merged.leaked, threshold, merged.ipIndex, depth + 1);
                }
            }
            writeSkipped();
        }
    }

    void handleAllocation() override
    {
        if (leaked > lastMassifPeak && massifOut.is_open()) {
            massifAllocations = allocations;
            lastMassifPeak = leaked;
        }
    }

    void handleTimeStamp(size_t /*newStamp*/, size_t oldStamp) override
    {
        if (massifOut.is_open()) {
            writeMassifSnapshot(oldStamp, oldStamp == totalTime);
        }
    }

    void handleDebuggee(const char* command) override
    {
        if (massifOut.is_open()) {
            writeMassifHeader(command);
        }
    }

    size_t massifSnapshotId = 0;
    size_t lastMassifPeak = 0;
    vector<Allocation> massifAllocations;
};

int main(int argc, char** argv)
{
    po::options_description desc("Options");
    desc.add_options()
        ("file,f", po::value<string>(),
            "The heaptrack data file to print.")
        ("shorten-templates,t", po::value<bool>()->default_value(true)->implicit_value(true),
            "Shorten template identifiers.")
        ("merge-backtraces,m", po::value<bool>()->default_value(true)->implicit_value(true),
            "Merge backtraces.\nNOTE: the merged peak consumption is not correct.")
        ("print-peaks,p", po::value<bool>()->default_value(true)->implicit_value(true),
            "Print backtraces to top allocators, sorted by peak consumption.")
        ("print-allocators,a", po::value<bool>()->default_value(true)->implicit_value(true),
            "Print backtraces to top allocators, sorted by number of calls to allocation functions.")
        ("print-leaks,l", po::value<bool>()->default_value(false)->implicit_value(true),
            "Print backtraces to leaked memory allocations.")
        ("print-overall-allocated,o", po::value<bool>()->default_value(false)->implicit_value(true),
            "Print top overall allocators, ignoring memory frees.")
        ("print-histogram,H", po::value<string>()->default_value(string()),
            "Path to output file where an allocation size histogram will be written to.")
        ("print-massif,M", po::value<string>()->default_value(string()),
            "Path to output file where a massif compatible data file will be written to.")
        ("massif-threshold", po::value<double>()->default_value(1.),
            "Percentage of current memory usage, below which allocations are aggregated into a 'below threshold' entry.\n"
            "This is only used in the massif output file so far.\n")
        ("massif-detailed-freq", po::value<size_t>()->default_value(2),
            "Frequency of detailed snapshots in the massif output file. Increase this to reduce the file size.\n"
            "You can set the value to zero to disable detailed snapshots.\n")
        ("filter-bt-function", po::value<string>()->default_value(string()),
            "Only print allocations where the backtrace contains the given function.")
        ("help,h",
            "Show this help message.")
        ("version,v",
            "Displays version information.");
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
            return 0;
        } else if (vm.count("version")) {
            cout << "heaptrack_print " << HEAPTRACK_VERSION_STRING << endl;
            return 0;
        }
        po::notify(vm);
    } catch (const po::error& error) {
        cerr << "ERROR: " << error.what() << endl
             << endl << desc << endl;
        return 1;
    }

    if (!vm.count("file")) {
        // NOTE: stay backwards compatible to old boost 1.41 available in RHEL 6
        //       otherwise, we could simplify this by setting the file option
        //       as ->required() using the new 1.42 boost API
        cerr << "ERROR: the option '--file' is required but missing\n\n" << desc << endl;
        return 1;
    }

    Printer data;

    const auto inputFile = vm["file"].as<string>();
    data.shortenTemplates = vm["shorten-templates"].as<bool>();
    data.mergeBacktraces = vm["merge-backtraces"].as<bool>();
    data.filterBtFunction = vm["filter-bt-function"].as<string>();
    const string printHistogram = vm["print-histogram"].as<string>();
    data.printHistogram = !printHistogram.empty();
    const string printMassif = vm["print-massif"].as<string>();
    if (!printMassif.empty()) {
        data.massifOut.open(printMassif, ios_base::out);
        if (!data.massifOut.is_open())  {
            cerr << "Failed to open massif output file \"" << printMassif << "\"." << endl;
            return 1;
        }
        data.massifThreshold = vm["massif-threshold"].as<double>();
        data.massifDetailedFreq = vm["massif-detailed-freq"].as<size_t>();
    }
    const bool printLeaks = vm["print-leaks"].as<bool>();
    const bool printOverallAlloc = vm["print-overall-allocated"].as<bool>();
    const bool printPeaks = vm["print-peaks"].as<bool>();
    const bool printAllocs = vm["print-allocators"].as<bool>();

    cout << "reading file \"" << inputFile << "\" - please wait, this might take some time..." << endl;
    if (!data.read(inputFile)) {
        return 1;
    }

    cout << "finished reading file, now analyzing data:\n" << endl;

    if (printAllocs) {
        // sort by amount of allocations
        cout << "MOST CALLS TO ALLOCATION FUNCTIONS\n";
        data.printAllocations(&AllocationData::allocations, [] (const AllocationData& data) {
            cout << data.allocations << " calls to allocation functions with " << formatBytes(data.peak) << " peak consumption from\n";
        }, [] (const AllocationData& data) {
            cout << data.allocations << " calls with " << formatBytes(data.peak) << " peak consumption from:\n";
        });
        cout << endl;
    }

    if (printOverallAlloc) {
        cout << "MOST BYTES ALLOCATED OVER TIME (ignoring deallocations)\n";
        data.printAllocations(&AllocationData::allocated, [] (const AllocationData& data) {
            cout << formatBytes(data.allocated) << " allocated over " << data.allocations << " calls from\n";
        }, [] (const AllocationData& data) {
            cout << formatBytes(data.allocated) << " allocated over " << data.allocations << " calls from:\n";
        });
        cout << endl;
    }

    if (printPeaks) {
        ///FIXME: find a way to merge this without breaking temporal dependency.
        /// I.e. a given function could be called N times from different places
        /// and allocate M bytes each, but free it thereafter.
        /// Then the below would give a wrong total peak size of N * M instead
        /// of just N!
        cout << "PEAK MEMORY CONSUMERS\n";
        if (data.mergeBacktraces) {
            cout << "\nWARNING - the data below is not an accurate calcuation of"
                    " the total peak consumption and can easily be wrong.\n"
                    " For an accurate overview, disable backtrace merging.\n";
        }

        data.printAllocations(&AllocationData::peak, [] (const AllocationData& data) {
            cout << formatBytes(data.peak) << " peak memory consumed over " << data.allocations << " calls from\n";
        }, [] (const AllocationData& data) {
            cout << formatBytes(data.peak) << " consumed over " << data.allocations << " calls from:\n";
        });
    }

    if (printLeaks) {
        // sort by amount of leaks
        cout << "MEMORY LEAKS\n";
        data.printAllocations(&AllocationData::leaked, [] (const AllocationData& data) {
            cout << formatBytes(data.leaked) << " leaked over " << data.allocations << " calls from\n";
        }, [] (const AllocationData& data) {
            cout << formatBytes(data.leaked) << " leaked over " << data.allocations << " calls from:\n";
        });
        cout << endl;
    }

    const double totalTimeS = 0.001 * data.totalTime;
    cout << "total runtime: " << fixed << totalTimeS << "s.\n"
         << "bytes allocated in total (ignoring deallocations): " << formatBytes(data.totalAllocated)
            << " (" << formatBytes(data.totalAllocated / totalTimeS) << "/s)" << '\n'
         << "calls to allocation functions: " << data.totalAllocations
            << " (" << size_t(data.totalAllocations / totalTimeS) << "/s)\n"
         << "peak heap memory consumption: " << formatBytes(data.peak) << '\n'
         << "total memory leaked: " << formatBytes(data.leaked) << '\n';

    if (!printHistogram.empty()) {
        ofstream histogram(printHistogram, ios_base::out);
        if (!histogram.is_open()) {
            cerr << "Failed to open histogram output file \"" << printHistogram << "\"." << endl;
        } else {
            for (auto entry : data.sizeHistogram) {
                histogram << entry.first << '\t' << entry.second << '\n';
            }
        }
    }

    return 0;
}
