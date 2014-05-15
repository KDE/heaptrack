#!/bin/bash

if [[ -z "$1" ]]; then
    echo "$0 DEBUGEE ARGS"
    exit 1
fi

output=$(pwd)/malloctrace.$1

cb
LD_PRELOAD=./libmalloctrace.so DUMP_MALLOC_TRACE_OUTPUT="$output" $@ &
pid=$!
wait $pid
# bzip2 "$output.$pid"
