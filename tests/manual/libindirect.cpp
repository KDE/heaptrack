#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <dlfcn.h>

extern "C" {
extern void allocFromLib(bool leak)
{
    fprintf(stderr, "malloc address: %p\n", dlsym(RTLD_NEXT, "malloc"));
    fprintf(stderr, "free address: %p\n", dlsym(RTLD_NEXT, "free"));

    auto ret = strdup("my long string that I want to copy");
    fprintf(stderr, "string is: %s\n", ret);
    if (!leak) {
        free(ret);
    }

    auto p = malloc(10);
    fprintf(stderr, "p = %p\n", p);

    if (!leak) {
        free(p);
    }
}
}
