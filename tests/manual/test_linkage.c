/*
 * Copyright 2018 Ivan Middleton <TODO>
 * Copyright 2018 Milian Wolff <mail@milianw.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/*
    By default, the linker uses lazy binding (function calls aren't
    resolved until the first time the function is called).

    Relevant sections of the executable for lazy binding:
    .plt      (trampoline code)
    .got.plt  (function addresses cached here)
    .rela.plt (relocation entries associating each function name
                with its storage location in .got.plt)

    But symbols can also be bound right away when the executable or
    shared library is started or loaded.

    Relevant sections for immediate binding:
    .plt.got  (trampoline code)
    .got      (function addresses stored here)
    .rela.dyn (relocation entries)

    Immediate binding can be triggered in a couple different ways:

    (1) The linker option "-z now" makes all symbols use immediate
        binding. Compile this file as follows to see this in action:

            gcc -Wl,-z,now -o testbind testbind.c

        Why might this linker option be used? See:

    https://wiki.debian.org/Hardening#DEB_BUILD_HARDENING_BINDNOW_.28ld_-z_now.29

        Note that this seems to be platform dependant and is not always reproducible.

    (2) If a particular function has a pointer to it passed around,
        then it must be bound immediately. Define TAKE_ADDR, i.e. compile with

            gcc -g -O0 -fuse-ld=bfd -DTAKE_ADDR -o testbind testbind.c

        to see this behavior. Note that ld.gold does not show this behavior.

    The heaptrack_inject function needs to look in both .rela.plt
    (DT_JMPREL) and .rela.dyn (DT_RELA) in order to find all
    malloc/free function pointers, lazily-bound or no.

    There is also another option which is currently not handled by heaptrack:
    When do not rewrite data segments, which would be required to catch accessing
    a given symbol through a function pointer (-DUSE_FREEPTR).

    Use the run_linkage_tests.sh bash script to check the behavior in an automated fashion.
*/

#include <stdlib.h>
#include <unistd.h>

#include <benchutil.h>

int main()
{
    // NOTE: if we'd read the free ptr here, then we would not catch/override the value by heaptrack
    int i = 0;

    sleep(1);

    for (i = 0; i < 10; ++i) {
        void* foo = malloc(256);
        escape(foo);

#if defined(TAKE_ADDR) || defined(USE_FREEPTR)
    // but when we read it here, heaptrack may have rewritten the value properly
    void (*freePtr)(void*) = &free;

    escape(freePtr);
#endif

        usleep(200);
#if defined(USE_FREEPTR)
        freePtr(foo);
#else
        free(foo);
#endif
        usleep(200);
    }
    return 0;
}
