# heaptrack - a heap memory profiler for Linux

Heaptrack traces all memory allocations and annotates these events with stack traces.
Dedicated analysis tools then allow you to interpret the heap memory profile to:

- find hotspots that need to be optimized to reduce the **memory footprint** of your application
- find **memory leaks**, i.e. locations that allocate memory which is never deallocated
- find **allocation hotspots**, i.e. code locations that trigger a lot of memory allocation calls
- find **temporary allocations**, which are allocations that are directly followed by their deallocation

## Using heaptrack

The recommended way is to launch your application and start tracing from the beginning:

    heaptrack <your application and its parameters>

    heaptrack output will be written to "/tmp/heaptrack.APP.PID.gz"
    starting application, this might take some time...

    ...

    heaptrack stats:
        allocations:            65
        leaked allocations:     60
        temporary allocations:  1

    Heaptrack finished! Now run the following to investigate the data:

        heaptrack_gui "/tmp/heaptrack.APP.PID.gz"

Alternatively, you can attach to an already running process:

    heaptrack --pid $(pidof <your application>)

    heaptrack output will be written to "/tmp/heaptrack.APP.PID.gz"
    injecting heaptrack into application via GDB, this might take some time...
    injection finished

    ...

    Heaptrack finished! Now run the following to investigate the data:

        heaptrack_gui "/tmp/heaptrack.APP.PID.gz"

## Interpreting the heap profile

Heaptrack generates data files that are impossible to analyze for a human. Instead, you need
to use either `heaptrack_print` or `heaptrack_gui` to interpret the results.

### heaptrack_gui

The highly recommended way to analyze a heap prfile is by using the `heaptrack_gui` tool.
It depends on Qt 5 and KF 5 to graphically visualize the recorded data. It features:

- a summary page of the data
- bottom-up and top-down tree views of the code locations that allocated memory with
  their aggregated cost and stack traces
- flame graph visualization
- graphs of allocation costs over time

### heaptrack_print

The `heaptrack_print` tool is a command line application with minimal dependencies. It takes
the heap profile, analyzes it, and prints the results in ASCII format to the command line.

In its most simple form, you can use it like this:

    heaptrack_print heaptrack.APP.PID.gz | less

By default, the report will contain three sections:

    MOST CALLS TO ALLOCATION FUNCTIONS
    PEAK MEMORY CONSUMERS
    MOST TEMPORARY ALLOCATIONS

Each section then lists the top ten hotspots, i.e. code locations that triggered e.g.
the most memory allocations.

Have a look at `heaptrack_print --help` for changing the output format and other options.

Note that you can use this tool to convert a heaptrack data file to the Massif data format.
You can generate a collapsed stack report for consumption by `flamegraph.pl`.

## Comparison to Valgrind's massif

The idea to build heaptrack was born out of the pain in working with Valgrind's massif.
Valgrind comes with a huge overhead in both memory and time, which sometimes prevent you
from running it on larger real-world applications. Most of what Valgrind does is not
needed for a simple heap profiler.

### Advantages of heaptrack over massif

- *speed and memory overhead*

  Multi-threaded applications are not serialized when you trace them with heaptrack and
  even for single-threaded applications the overhead in both time and memory is significantly
  lower. Most notably, you only pay a price when you allocate memory -- time-intensive CPU
  calculations are not slowed down at all, contrary to what happens in Valgrind.

- *more data*

  Valgrind's massif aggregates data before writing the report. This step loses a lot of
  useful information. Most notably, you are not longer able to find out how often memory
  was allocated, or where temporary allocations are triggered. Heaptrack does not aggregate the
  data until you interpret it, which allows for more useful insights into your allocation patterns.

### Advantages of massif over heaptrack

- *ability to profile page allocations as heap*

  This allows you to heap-profile applications that use pool allocators that circumvent
  malloc & friends. Heaptrack can in principle also profile such applications, but it
  requires code changes to annotate the memory pool implementation.

- *ability to profile stack allocations*

  This is inherently impossible to implement efficiently in heaptrack as far as I know.
