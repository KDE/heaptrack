#!/bin/sh

#
# SPDX-FileCopyrightText: 2024 Shivam Kunwar <shivam.kunwar@kdab.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#

set -e

SRC_DIR="@CMAKE_CURRENT_SOURCE_DIR@/test_sysroot"
BIN_DIR="@PROJECT_BINARY_DIR@/@LIBEXEC_INSTALL_DIR@"

if [ ! -d "$SRC_DIR" ] || [ ! -d "$BIN_DIR" ]; then
    echo "failed to find SRC_DIR/BIN_DIR - do you run this from the build dir?"
    echo "SRC_DIR: $SRC_DIR"
    echo "BIN_DIR: $BIN_DIR"
    exit 1
fi;

temp_output_actual=$(mktemp)
trap 'rm -- "$temp_output_actual"' EXIT

unset DEBUGINFOD_URLS
"$BIN_DIR/heaptrack_interpret" \
    --sysroot "${SRC_DIR}/sysroot" \
    --extra-paths "${SRC_DIR}/extra" \
    --debug-paths "${SRC_DIR}/debug" \
    < "${SRC_DIR}/heaptrack.test_sysroot.raw" \
    > "$temp_output_actual"

# verification step
if diff -u "${SRC_DIR}/heaptrack.test_sysroot.expected" "$temp_output_actual"; then
    echo "Test passed: Output matches expected result."
    exit 0
else
    echo "Test failed: Output does not match expected result."
    exit 1
fi
