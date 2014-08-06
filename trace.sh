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

output=$(pwd)/heaptrack.$(basename $1).$$

cb

echo "starting application, this might take some time..."

pipe=/tmp/heaptrack_fifo$$
mkfifo $pipe
trap "rm -f $pipe" EXIT

./heaptrack_interpret < $pipe | gzip -c > "$output.gz" &
compressor=$!

if [ -z "$debug" ]; then
  LD_PRELOAD=./libheaptrack.so DUMP_HEAPTRACK_OUTPUT="$pipe" $@
else
  gdb --eval-command="set environment LD_PRELOAD=./libheaptrack.so" \
      --eval-command="set environment DUMP_HEAPTRACK_OUTPUT=$pipe" \
      --eval-command="run" --args $@
fi

wait $compressor
