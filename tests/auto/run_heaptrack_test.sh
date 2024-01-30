#!/bin/sh

#
# SPDX-FileCopyrightText: 2024 Shivam Kunwar <shivam.kunwar@kdab.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#

MOCK_SYSROOT="${SRC_DIR}/mock_sysroot"

zstd -dc < "${SRC_DIR}/heaptrack.mock_executable.570491.raw.zst" \
| "${INTP_LIB}/lib/heaptrack/libexec/heaptrack_interpret" --sysroot "${MOCK_SYSROOT}" \
| zstd -c > "${SRC_DIR}/heaptrack.mock_executable.570491.zst"

# verification step
zstd -dc < "${SRC_DIR}/heaptrack.mock_executable.570491.zst" > temp_output
zstd -dc < "${SRC_DIR}/expected_output.zst" > expected_output

if diff -u temp_output expected_output; then
    echo "Test passed: Output matches expected result."
    rm temp_output expected_output
    exit 0
else
    echo "Test failed: Output does not match expected result."
    rm temp_output expected_output
    exit 1
fi
