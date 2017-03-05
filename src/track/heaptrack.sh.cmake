#!/bin/sh

#
# Copyright 2014-2017 Milian Wolff <mail@milianw.de>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU Library General Public License as
# published by the Free Software Foundation; either version 2 of the
# License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public
# License along with this program; if not, write to the
# Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
#

usage() {
    echo "Usage: $0 [--debug|-d] DEBUGGEE [ARGUMENT]..."
    echo "or:    $0 [--debug|-d] -p PID"
    echo
    echo "A heap memory usage profiler. It uses LD_PRELOAD to track all"
    echo "calls to the core memory allocation functions and logs these"
    echo "occurrances. Additionally, backtraces are obtained and logged."
    echo "Combined this can give interesting answers to questions such as:"
    echo
    echo "  * How much heap memory is my application using?"
    echo "  * Where is heap memory being allocated, and how often?"
    echo "  * How much space are heap individual allocations requesting?"
    echo
    echo "To evaluate the generated heaptrack data, use heaptrack_print or heaptrack_gui."
    echo
    echo "Mandatory arguments to heaptrack:"
    echo "  DEBUGGEE       The name or path to the application that should"
    echo "                 be run with heaptrack analyzation enabled."
    echo
    echo "Alternatively, to attach to a running process:"
    echo "  -p, --pid PID  The process ID of a running process into which"
    echo "                 heaptrack will be injected. This only works with"
    echo "                 applications that already link against libdl."
    echo "  WARNING: Runtime-attaching heaptrack is UNSTABLE and can lead to CRASHES"
    echo "           in your application, especially after you detach heaptrack again."
    echo "           You are hereby warned, use it at your own risk!"
    echo
    echo "Optional arguments to heaptrack:"
    echo "  -d, --debug    Run the debuggee in GDB and heaptrack."
    echo "  ARGUMENT       Any number of arguments that will be passed verbatim"
    echo "                 to the debuggee."
    echo "  -h, --help     Show this help message and exit."
    echo "  -v, --version  Displays version information."
    echo
    exit 0
}

debug=
pid=
client=

while true; do
    case "$1" in
        "-d" | "--debug")
            debug=1
            shift 1
            ;;
        "-h" | "--help")
            usage
            exit 0
            ;;
        "-p" | "--pid")
            pid=$2
            if [ -z "$pid" ]; then
                echo "Missing PID argument."
                exit 1
            fi
            client=$(cat /proc/$pid/comm)
            if [ -z "$client" ]; then
                echo "Cannot attach to unknown process with PID $pid."
                exit 1
            fi
            shift 2
            echo $@
            if [ ! -z "$@" ]; then
                echo "You cannot specify a debuggee and a pid at the same time."
                exit 1
            fi
            break
            ;;
        "-v" | "--version")
            echo "heaptrack @HEAPTRACK_VERSION_MAJOR@.@HEAPTRACK_VERSION_MINOR@.@HEAPTRACK_VERSION_PATCH@"
            exit 0
            ;;
        *)
            if [ "$1" = "--" ]; then
                shift 1
            fi
            if [ ! -x "$(which "$1" 2> /dev/null)" ]; then
                echo "Error: Debuggee \"$1\" is not an executable."
                echo
                echo "Usage: $0 [--debug|-d] [--help|-h] DEBUGGEE [ARGS...]"
                exit 1
            fi
            client="$1"
            shift 1
            break
            ;;
    esac
done

# put output into current pwd
output=$(pwd)/heaptrack.$(basename "$client").$$

# find preload library and interpreter executable using relative paths
EXE_PATH=$(readlink -f $(dirname $(readlink -f $0)))
LIB_REL_PATH="@LIB_REL_PATH@"
LIBEXEC_REL_PATH="@LIBEXEC_REL_PATH@"

INTERPRETER="$EXE_PATH/$LIBEXEC_REL_PATH/heaptrack_interpret"
if [ ! -f "$INTERPRETER" ]; then
    echo "Could not find heaptrack interpreter executable: $INTERPRETER"
    exit 1
fi
INTERPRETER=$(readlink -f "$INTERPRETER")

LIBHEAPTRACK_PRELOAD="$EXE_PATH/$LIB_REL_PATH/libheaptrack_preload.so"
if [ ! -f "$LIBHEAPTRACK_PRELOAD" ]; then
    echo "Could not find heaptrack preload library $LIBHEAPTRACK_PRELOAD"
    exit 1
fi
LIBHEAPTRACK_PRELOAD=$(readlink -f "$LIBHEAPTRACK_PRELOAD")

LIBHEAPTRACK_INJECT="$EXE_PATH/$LIB_REL_PATH/libheaptrack_inject.so"
if [ ! -f "$LIBHEAPTRACK_INJECT" ]; then
    echo "Could not find heaptrack inject library $LIBHEAPTRACK_INJECT"
    exit 1
fi
LIBHEAPTRACK_INJECT=$(readlink -f "$LIBHEAPTRACK_INJECT")

# setup named pipe to read data from
pipe=/tmp/heaptrack_fifo$$
mkfifo $pipe

# interpret the data and compress the output on the fly
output="$output.gz"
"$INTERPRETER" < $pipe | gzip -c > "$output" &
debuggee=$!

cleanup() {
    if [ ! -z "$pid" ]; then
        echo "removing heaptrack injection via GDB, this might take some time..."
        gdb --batch-silent -n -iex="set auto-solib-add off" -p $pid \
            --eval-command="sharedlibrary libheaptrack_inject" \
            --eval-command="call heaptrack_stop()" \
            --eval-command="detach"
        # NOTE: we do not call dlclose here, as that has the tendency to trigger
        #       crashes in the debuggee. So instead, we keep heaptrack loaded.
    fi
    rm -f "$pipe"
    kill "$debuggee" 2> /dev/null

    echo "Heaptrack finished! Now run the following to investigate the data:"
    echo
    if [ "$(which heaptrack_gui 2> /dev/null)" != "" ]; then
        echo "  heaptrack_gui \"$output\""
    else
        echo "  heaptrack_print \"$output\" | less"
    fi
}
trap cleanup EXIT

echo "heaptrack output will be written to \"$output\""

if [ -z "$debug" ] && [ -z "$pid" ]; then
  echo "starting application, this might take some time..."
  LD_PRELOAD=$LIBHEAPTRACK_PRELOAD${LD_PRELOAD:+:$LD_PRELOAD} DUMP_HEAPTRACK_OUTPUT="$pipe" "$client" "$@"
else
  if [ -z "$pid" ]; then
    echo "starting application in GDB, this might take some time..."
    gdb --eval-command="set environment LD_PRELOAD=$LIBHEAPTRACK_PRELOAD" \
        --eval-command="set environment DUMP_HEAPTRACK_OUTPUT=$pipe" \
        --eval-command="run" --args "$client" "$@"
  else
    echo "injecting heaptrack into application via GDB, this might take some time..."
    gdb --batch-silent -n -iex="set auto-solib-add off" -p $pid \
        --eval-command="sharedlibrary libc.so" \
        --eval-command="call (void) __libc_dlopen_mode(\"$LIBHEAPTRACK_INJECT\", 0x80000000 | 0x002)" \
        --eval-command="sharedlibrary libheaptrack_inject" \
        --eval-command="call (void) heaptrack_inject(\"$pipe\")" \
        --eval-command="detach"
    echo "injection finished"
  fi
fi

wait $debuggee

# kate: hl Bash
