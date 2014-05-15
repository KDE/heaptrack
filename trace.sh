#!/bin/bash

debug=
if [[ "$1" == "--debug" ]]; then
  debug=1
  shift 1;
fi

if [[ -z "$1" ]]; then
    echo "$0 DEBUGEE ARGS"
    exit 1
fi

output=$(pwd)/malloctrace.$1

cb
if [ -z "$debug" ]; then
  LD_PRELOAD=./libmalloctrace.so DUMP_MALLOC_TRACE_OUTPUT="$output" $@ &
  pid=$!
  wait $pid
  #bzip2 "$output.$pid"
else
  gdb --eval-command="set environment LD_PRELOAD=./libmalloctrace.so" --eval-command="run" --args $@
fi
