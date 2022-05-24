/*
    SPDX-FileCopyrightText: 2022 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <dlfcn.h>

extern "C" {
__attribute__((weak)) void* __libc_dlopen_mode(const char* filename, int flag);
__attribute__((weak)) void* dlmopen(Lmid_t lmid, const char* filename, int flags);
}

namespace {
void dlopenLine(const char* lib)
{
#ifdef __FreeBSD__

    fprintf(stdout, "'dlopen@plt'(\"%s\", 0x%x)\n", lib, RTLD_NOW);

#else

    if (&__libc_dlopen_mode) {
        // __libc_dlopen_mode was available directly in glibc before libdl got merged into it
        fprintf(stdout, "__libc_dlopen_mode(\"%s\", 0x80000000 | 0x002)\n", lib);
    } else if (&dlmopen) {
        fprintf(stdout, "dlmopen(0x%x, \"%s\", 0x%x)\n", LM_ID_BASE, lib, RTLD_NOW);
    } else {
        fprintf(stdout, "dlopen(\"%s\", 0x%x)\n", lib, RTLD_NOW);
    }

#endif
}
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "missing check\n");
        return EXIT_FAILURE;
    }

    const auto check = argv[1];
    if (strcmp(check, "dlopen") == 0) {
        if (argc != 3) {
            fprintf(stderr, "missing lib arg\n");
            return EXIT_FAILURE;
        }
        dlopenLine(argv[2]);
        return EXIT_SUCCESS;
    }

    fprintf(stderr, "unsupported check %s\n", check);
    return EXIT_FAILURE;
}
