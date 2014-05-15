#!/bin/bash

if [[ -z "$2" ]]; then
    echo "$0 INTERVAL DEBUGEE ARGS"
    exit 1
fi

interval=$1
shift 1

output=$(pwd)/mallocinfo.$1

cb
LD_PRELOAD=./libdumpmallocinfo.so DUMP_MALLOC_INFO_INTERVAL="$interval" DUMP_MALLOC_INFO_OUTPUT="$output" $@ &
pid=$!
wait $pid
bzip2 "$output.$pid"
