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

output=$(pwd)/malloctrace.$(basename $1).

cb

echo "starting application, this might take some time..."

if [ -z "$debug" ]; then
  LD_PRELOAD=./libmalloctrace.so DUMP_MALLOC_TRACE_OUTPUT="$output" $@ &
else
  gdb --eval-command="set environment LD_PRELOAD=./libmalloctrace.so" \
      --eval-command="set environment DUMP_MALLOC_TRACE_OUTPUT=\"$output\"" \
      --eval-command="run" --args $@
fi

pid=$!
wait $pid

if [[ "$(ls $output$pid 2> /dev/null)" != "" ]]; then
    echo "finished application, zipping data file"
    cd $(dirname $output)
    output=$(basename $output)$pid
    bzip2 "$output"
    du -h "$output.bz2"
fi
