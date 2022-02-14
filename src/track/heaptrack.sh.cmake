#!/bin/sh

#
# SPDX-FileCopyrightText: 2014-2021 Milian Wolff <mail@milianw.de>
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#

usage() {
    echo "Usage: $0 [--debug|-d] [--use-inject] DEBUGGEE [ARGUMENT]..."
    echo "or:    $0 [--debug|-d] -p PID"
    echo "or:    $0 -a FILE"
    echo
    echo "A heap memory usage profiler. It uses LD_PRELOAD to track all"
    echo "calls to the core memory allocation functions and logs these"
    echo "occurrences. Additionally, backtraces are obtained and logged."
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
    echo "  -r, --raw      Only record raw data, do not interpret it."
    echo "  -d, --debug    Run the debuggee in GDB and heaptrack."
    echo " --use-inject    Use the same heaptrack_inject symbol interception mechanism instead of relying on"
    echo "                 the dynamic linker and LD_PRELOAD. This is an experimental flag for now."
    echo "  ARGUMENT       Any number of arguments that will be passed verbatim"
    echo "                 to the debuggee."
    echo "  -h, --help     Show this help message and exit."
    echo "  -v, --version  Displays version information."
    echo "  -o, --output   Specifies the data-file for the captured data."
    echo "                 %h in the file name string is replaced with the hostname of the system."
    echo "                 %p in the file name string is replaced with the pid of the application being profiled."
    echo "                 Parent directories will be created if output files are under non-existing directories."
    echo "                 e.g.,"
    echo "                   ./%h/%p/outdat will be translated into ./<hostname>/<pid>/outdat."
    echo "                   The directory ./<hostname>/<pid> will be created if it doesn't exist."
    echo
    echo "Alternatively, to analyze a recorded heaptrack data file:"
    echo "  -a, --analyze FILE    Open the heaptrack data file in heaptrack_gui, if available,"
    echo "                        or fallback to heaptrack_print otherwise."
    echo "                        Any options passed after --analyze will be passed along."
    echo
    exit 0
}

debug=
pid=
client=
use_inject_lib=
write_raw_data=

# path to current heaptrack.sh executable
SCRIPT_PATH=$(readlink -f "$0")
SCRIPT_DIR=$(dirname "$SCRIPT_PATH")
EXE_PATH=$(readlink -f "$SCRIPT_DIR")

openHeaptrackDataFiles() {
    if [ -x "$EXE_PATH/heaptrack_gui" ]; then
        "$EXE_PATH/heaptrack_gui" "$@"
    else
        "$EXE_PATH/heaptrack_print" "$@"
    fi
}

ORIG_CMDLINE=$@

while true; do
    case "$1" in
        "-d" | "--debug")
            if [ -z "$(command -v gdb 2> /dev/null)" ]; then
                echo "GDB is not installed, cannot debug heaptrack."
                exit 1
            fi
            debug=1
            shift 1
            ;;
        "--use-inject")
            use_inject_lib=1
            shift 1
            ;;
        "-r" | "--raw")
            write_raw_data=1
            shift 1
            ;;
        "-h" | "--help")
            usage
            exit 0
            ;;
        "-o" | "--output" | "--output-file")
            if [ -z "$2" ]; then
                echo "Missing output argument."
                exit 1
            fi
            output=$(echo $2 | sed "s/%h/$(hostname)/g" | sed "s/%p/$$/g")
            if [ -d "$output" ]; then
                echo "Please specify a file-name or a full path-name for output."
                exit 1
            fi
            if [ ! -d $(dirname $output) ]; then
              mkdir -p $(dirname $output)
            fi
            output=$(readlink -f $2)
            shift 2
            ;;
        "-p" | "--pid")
            if [ -z "$(command -v gdb 2> /dev/null)" ]; then
                echo "GDB is not installed, cannot attach to running process."
                exit 1
            fi
            if [ -f "/proc/sys/kernel/yama/ptrace_scope"  ] && [ "$(cat "/proc/sys/kernel/yama/ptrace_scope")" -gt "0" ]; then
                echo "Cannot runtime-attach, you need to set /proc/sys/kernel/yama/ptrace_scope to 0"
                exit 1
            fi
            pid=$2
            if [ -z "$pid" ]; then
                echo "Missing PID argument."
                exit 1
            fi
            case $(uname) in
                Linux*)
                    client=$(cat "/proc/$pid/comm")
                ;;
                FreeBSD*)
                    client=$(awk '{print $1}' < "/proc/$pid/cmdline")
                ;;
            esac
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
        "-a" | "--analyze")
            shift 1
            openHeaptrackDataFiles "$@"
            exit
            ;;
        *)
            if [ "$1" = "--" ]; then
                shift 1
            fi
            if [ ! -x "$(command -v "$1" 2> /dev/null)" ]; then
                if [ -z "$1" ] && [ -x "$EXE_PATH/heaptrack_gui" ]; then
                    "$EXE_PATH/heaptrack_gui"
                    exit
                fi
                if [ -f "$1" ] && echo "$1" | grep -q "heaptrack."; then
                    openHeaptrackDataFiles "$ORIG_CMDLINE"
                    exit
                fi

                if [ ! -e "$1" ]; then
                    echo "Error: Debuggee \"$1\" was not found."
                else
                    echo "Error: Debuggee \"$1\" is not an executable."
                fi

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
if [ -z "$output" ]; then
    output=$(pwd)/heaptrack.$(basename "$client").$$
fi

# find preload library and interpreter executable using relative paths
LIB_REL_PATH="@LIB_REL_PATH@"
LIBEXEC_REL_PATH="@LIBEXEC_REL_PATH@"

INTERPRETER="$EXE_PATH/$LIBEXEC_REL_PATH/heaptrack_interpret"
if [ -z "$write_raw_data" ] && [ ! -f "$INTERPRETER" ]; then
    echo "Could not find heaptrack interpreter executable: $INTERPRETER"
    exit 1
fi
INTERPRETER=$(readlink -f "$INTERPRETER")

if [ -z "$use_inject_lib" ]; then
    LIBHEAPTRACK_PRELOAD="$EXE_PATH/$LIB_REL_PATH/libheaptrack_preload.so"
else
    LIBHEAPTRACK_PRELOAD="$EXE_PATH/$LIB_REL_PATH/libheaptrack_inject.so"
fi
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

# if root is profiling a process for non root
# give profiled process write access to the pipe
if [ ! -z "$pid" ]; then
  case $(uname) in
    Linux*)
      pid_user=$(stat -c %U "/proc/$pid")
    ;;
    FreeBSD*)
      pid_user=$(stat -f %Su "/proc/$pid")
    ;;
  esac
  if [ -z "$pid_user" ]; then
    exit 1
  fi
  chown "$pid_user" "$pipe" || exit 1
fi

output_suffix="gz"
COMPRESSOR="gzip -c"
UNCOMPRESSOR="gzip -dc"

if [ "@ZSTD_FOUND@" = "TRUE" ] && [ ! -z "$(command -v zstd 2> /dev/null)" ]; then
    output_suffix="zst"
    COMPRESSOR="zstd -c"
    UNCOMPRESSOR="zstd -dc"
fi

output_non_raw="$output.$output_suffix"

if [ ! -z "$write_raw_data" ]; then
    output_suffix="raw.$output_suffix"
fi

# interpret the data and compress the output on the fly
output="$output.$output_suffix"
if [ -z "$write_raw_data" ]; then
    "$INTERPRETER" < $pipe | $COMPRESSOR > "$output" &
else
    $COMPRESSOR < $pipe > "$output" &
fi
debuggee=$!

cleanup() {
    if [ ! -z "$pid" ] && [ -d "/proc/$pid" ]; then
        echo "removing heaptrack injection via GDB, this might take some time..."
        gdb --batch-silent -n -iex="set auto-solib-add off" -p $pid \
            --eval-command="sharedlibrary libheaptrack_inject" \
            --eval-command="call heaptrack_stop()" \
            --eval-command="detach"
        # NOTE: we do not call dlclose here, as that has the tendency to trigger
        #       crashes in the debuggee. So instead, we keep heaptrack loaded.
    fi
    rm -f "$pipe"
    case $(uname) in
        FreeBSD*)
            rm -f "$pipe.lock"
        ;;
    esac
    kill "$debuggee" 2> /dev/null

    echo "Heaptrack finished! Now run the following to investigate the data:"
    echo

    if [ ! -z "$write_raw_data" ]; then
        echo "  $UNCOMPRESSOR < \"$output\" | $INTERPRETER | $COMPRESSOR > \"$output_non_raw\""
    else
        echo "  heaptrack --analyze \"$output\""
    fi

    if [ -z "$write_raw_data" ] && [ -x "$EXE_PATH/heaptrack_gui" ]; then
        echo ""
        echo "heaptrack_gui detected, automatically opening the file..."
        "$EXE_PATH/heaptrack_gui" "$output"
    fi
}
trap cleanup EXIT

echo "heaptrack output will be written to \"$output\""
echo "$LIBHEAPTRACK_PRELOAD"

if [ -z "$debug" ] && [ -z "$pid" ]; then
  echo "starting application, this might take some time..."
  LD_PRELOAD="$LIBHEAPTRACK_PRELOAD${LD_PRELOAD:+:$LD_PRELOAD}" DUMP_HEAPTRACK_OUTPUT="$pipe" "$client" "$@"
  EXIT_CODE=$?
else
  if [ -z "$pid" ]; then
    echo "starting application in GDB, this might take some time..."
    gdb --quiet --eval-command="set environment LD_PRELOAD=$LIBHEAPTRACK_PRELOAD" \
        --eval-command="set environment DUMP_HEAPTRACK_OUTPUT=$pipe" \
        --eval-command="set startup-with-shell off" \
        --eval-command="run" --args "$client" "$@"
    EXIT_CODE=$?
  else
    echo "injecting heaptrack into application via GDB, this might take some time..."
    case $(uname) in
        Linux*)
            dlopen=__libc_dlopen_mode
        ;;
        FreeBSD*)
            dlopen=dlopen@plt
        ;;
    esac
    if [ -z "$debug" ]; then
        gdb --batch-silent -n -iex="set auto-solib-add off" -p $pid \
            --eval-command="sharedlibrary libc.so" \
            --eval-command="call (void) '$dlopen'(\"$LIBHEAPTRACK_INJECT\", 0x80000000 | 0x002)" \
            --eval-command="sharedlibrary libheaptrack_inject" \
            --eval-command="call (void) heaptrack_inject(\"$pipe\")" \
            --eval-command="detach"
    else
        gdb --quiet -p $pid \
            --eval-command="sharedlibrary libc.so" \
            --eval-command="call (void) '$dlopen'(\"$LIBHEAPTRACK_INJECT\", 0x80000000 | 0x002)" \
            --eval-command="sharedlibrary libheaptrack_inject" \
            --eval-command="call (void) heaptrack_inject(\"$pipe\")"
    fi
    EXIT_CODE=$?
    echo "injection finished"
  fi
fi

wait $debuggee
exit $EXIT_CODE

# kate: hl Bash
