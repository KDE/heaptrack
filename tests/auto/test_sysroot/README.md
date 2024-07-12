The files in here are used for the sysroot-resolve auto test

- libtestlib.so is copied over directly to the path from my system
  this allows us to test resolving via `--sysroot`
- test_lib is put into another folder
  this allows us to test the `--extra-paths` feature
- then the debug info for a system lib referenced by the
  executable is copied to various places to test that finding
  them with the default path as well as the `--debug-paths` feature.
  These files get the `.debug` suffix too.

All files are stripped for safety purposes and since we don't
need the executable code anyhow for symbolication:

    strip --only-keep-debug ...

For libc, we want to test debuglink resolution. The original file is copied over via:

    objcopy /usr/lib/libc.so.6 -R .text -R .note.stapsdt -R .bss -R .data -R .data.rel.ro -R .rodata libc.so.6

The debug file is then looked up via:

    debuginfod-find debuginfo /usr/lib/libc.so.6
    /home/milian/.cache/debuginfod_client/32a656aa5562eece8c59a585f5eacd6cf5e2307b/debuginfo

This file is then copied to a place matching the layout of a `.debug` folder, in our case:

    .build-id/32/a656aa5562eece8c59a585f5eacd6cf5e2307b.debug

The `heaptrack.test_sysroot.raw` is manually reduced to not reference any extra files that
are not important for the sake of this test here.
