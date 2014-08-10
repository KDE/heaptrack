#!/bin/bash

#
# Copyright 2014 Milian Wolff <mail@milianw.de>
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

debug=
if [ "$1" = "--debug" ]; then
  debug=1
  shift 1;
fi

if [ -z "$1" ]; then
    echo "$0 DEBUGEE [ARGS...]"
    exit 1
fi

# put output into current pwd
output=$(pwd)/heaptrack.$(basename $1).$$

# find preload library and interpreter executable using relative paths
EXE_PATH=$(readlink -f $(dirname $0))
LIB_REL_PATH="@LIB_REL_PATH@"
LIBEXEC_REL_PATH="@LIBEXEC_REL_PATH@"

INTERPRETER="$EXE_PATH/$LIBEXEC_REL_PATH/heaptrack_interpret"
if [ ! -f "$INTERPRETER" ]; then
    echo "Could not find heaptrack interpreter executable: $INTERPRETER"
    exit 1
fi
INTERPRETER=$(readlink -f "$INTERPRETER")

LIBHEAPTRACK="$EXE_PATH/$LIB_REL_PATH/libheaptrack.so"
if [ ! -f "$LIBHEAPTRACK" ]; then
    echo "Could not find heaptrack preload library$LIBHEAPTRACK"
    exit 1
fi
LIBHEAPTRACK=$(readlink -f "$LIBHEAPTRACK")

# setup named pipe to read data from
pipe=/tmp/heaptrack_fifo$$
mkfifo $pipe
trap "rm -f $pipe" EXIT

# interpret the data and compress the output on the fly
output="$output.gz"
"$INTERPRETER" < $pipe | gzip -c > "$output" &
debuggee=$!

echo "starting application, this might take some time..."
echo "output will be written to $output"

if [ -z "$debug" ]; then
  LD_PRELOAD=$LIBHEAPTRACK DUMP_HEAPTRACK_OUTPUT="$pipe" $@
else
  gdb --eval-command="set environment LD_PRELOAD=$LIBHEAPTRACK" \
      --eval-command="set environment DUMP_HEAPTRACK_OUTPUT=$pipe" \
      --eval-command="run" --args $@
fi

wait $debuggee

echo "Heaptrack finished! Now run the following to investigate the data:"
echo
echo "  heaptrack_print $output | less"

# kate: hl Bash
