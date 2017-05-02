#
# A script to convert perf data files into the heaptrack format.
#
# perf script -s perf-heaptrack.py -i perf.data | gzip > perf.heaptrack.gz
# heaptrack_gui perf.heaptrack.gz
#
# This is mostly a proof-of-concept to show how this could be used
# in the future to visualize perf results.
#
# Copyright 2016-2017 Milian Wolff <mail@milianw.de>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
#

import os
import sys
import json
import subprocess
from collections import defaultdict

sys.path.append(os.environ["PERF_EXEC_PATH"] + "/scripts/python/Perf-Trace-Util/lib/Perf/Trace")

from perf_trace_context import *

try:
    from subprocess import DEVNULL # py3k
except ImportError:
    import os
    DEVNULL = open(os.devnull, 'wb')

class FileInfo:
    def __init__(self, file, line):
        self.file = file
        self.line = line

class InternMap:
    def __init__(self):
        self.map = dict()
        self.map[""] = 0;

    def add(self, string):
        nextIndex = len(self.map)
        index = self.map.get(string, nextIndex)
        if index == nextIndex:
            print("s %s" % string)
            self.map[string] = nextIndex
            return nextIndex
        return index

strings = InternMap()

def addr2line(dsoName, address):
    try:
        output = subprocess.check_output(["addr2line", "-e", dsoName, hex(address)], stderr=DEVNULL).split(':')
        file = output[0]
        if not file or file == "??":
            raise "error"
        line = int(output[1])
        return FileInfo(file, line)
    except:
        return FileInfo("???", 0)

nextIpIndex = 1
class InstructionPointerMap:
    def __init__(self):
        self.map = dict()

    def add(self, ip, dsoName, name, sym):
        ipEntry = self.map.get(ip, None)
        if ipEntry == None:
            global nextIpIndex
            ipEntry = nextIpIndex
            nextIpIndex += 1
            fileInfo = addr2line(dsoName, ip)
            print("i %x %x %x %x %d" % (ip, strings.add(dsoName), strings.add(name), strings.add(fileInfo.file), fileInfo.line))
            self.map[ip] = ipEntry
        return ipEntry
ipMap = InstructionPointerMap()

nextTraceIndex = 1
class TraceEntry:
    def __init__(self, traceIndex):
        self.index = traceIndex
        self.children = dict()

    def add(self, ipIndex):
        child = self.children.get(ipIndex, None)
        if child == None:
            global nextTraceIndex
            child = TraceEntry(nextTraceIndex)
            nextTraceIndex += 1
            print("t %x %x" % (ipIndex, self.index))
            self.children[ipIndex] = child
        return child

traceRoot = TraceEntry(0)

nextSampleIndex = 0
class SampleMap:
    def __init__(self):
        self.map = dict()

    def add(self, traceIndex):
        sampleIndex = self.map.get(traceIndex, None)
        if sampleIndex == None:
            global nextSampleIndex
            sampleIndex = nextSampleIndex
            nextSampleIndex += 1
            print("a 1 %x" % (traceIndex))
            self.map[traceIndex] = sampleIndex
        return sampleIndex
samples = SampleMap()

# write the callgrind file format to stdout
def trace_begin():
    print("v 10000")
    print("# perf.data converted using perf-heaptrack.py")

# this function gets called for every sample
# the event dict contains information about the symbol, time, callchain, ...
# print it out to peek inside!
startTime = 0
lastTime = 0
finalTime = 0

def trace_end():
    print("c %x" % (finalTime + 1))

def process_event(event):
    global startTime, lastTime, finalTime
    if startTime == 0:
        startTime = event["sample"]["time"]
    elapsed = (event["sample"]["time"] - startTime) / 10000000
    if (lastTime + 1) <= elapsed:
        print("c %x" % elapsed)
        lastTime = elapsed
    finalTime = elapsed

    global ipMap, traceRoot, samples
    trace = traceRoot
    if not event["callchain"]:
        dsoName = event.get("dso", "???")
        name = event.get("symbol", "???")
        ipIndex = ipMap.add(event["sample"]["ip"], dsoName, name, None)
        traceIndex = trace.add(ipIndex).index
    else:
        # add a function for every frame in the callchain
        for item in reversed(event["callchain"]):
            dsoName = item.get("dso", "???")
            name = "???"
            if "sym" in item:
                name = item["sym"]["name"]
            ipIndex = ipMap.add(item["ip"], dsoName, name, item.get("sym", None))
            trace = trace.add(ipIndex)
            traceIndex = trace.index
    sampleIndex = samples.add(traceIndex)
    print("+ %x" % sampleIndex)
