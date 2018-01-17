#!/bin/bash

input_file=$(readlink -f test_linkage.c)
cd /tmp

function run {
  echo "compiling with $@"
  gcc $@ -O0 -g $input_file -o test_linkage || return
  readelf -a test_linkage | grep free
  ./test_linkage &
  p=$!
  sleep .1
  heaptrack -p $p |& tee test_linkage.log
  if ! grep -qP "^\s+allocations:\s+10$" test_linkage.log; then
    echo "FAILED: wrong allocation count (compiled with $@)"
  fi
  if ! grep -qP "^\s+leaked allocations:\s+0$" test_linkage.log; then
    echo "FAILED: wrong allocation count (compiled with $@)"
  fi
  if ! grep -qP "^\s+temporary allocations:\s+10$" test_linkage.log; then
    echo "FAILED: wrong temporary allocation count (compiled with $@)"
  fi
}

# set -o xtrace

for linker in bfd gold; do
    run -fuse-ld=$linker
    run -fuse-ld=$linker -Wl,-z,now
    run -fuse-ld=$linker -DTAKE_ADDR
    run -fuse-ld=$linker -DUSE_FREEPTR
done

rm heaptrack.test_linkage.*.gz test_linkage test_linkage.log
