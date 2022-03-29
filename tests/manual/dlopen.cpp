#include <dlfcn.h>

#include <cstdio>
#include <cstdlib>

int main()
{
#ifndef RTLD_DEEPBIND
    printf("SKIP (RTLD_DEEPBIND undefined)\n");
#else
    fprintf(stderr, "malloc address: %p\n", dlsym(RTLD_NEXT, "malloc"));
    fprintf(stderr, "free address: %p\n", dlsym(RTLD_NEXT, "free"));

    auto p = malloc(10);
    fprintf(stderr, "p = %p\n", p);
    free(p);

    fprintf(stderr, "loading lib: %s\n", LIB_PATH);
    auto handle = dlopen(LIB_PATH, RTLD_DEEPBIND | RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        fprintf(stderr, "dlopen error loading %s: %s\n", LIB_PATH, dlerror());
        return 1;
    }

    auto allocFromLib = reinterpret_cast<void (*)(bool)>(dlsym(RTLD_NEXT, "allocFromLib"));
    if (!allocFromLib) {
        fprintf(stderr, "allocFromLib not resolved: %s\n", dlerror());
        return 2;
    }

    allocFromLib(false);

    fprintf(stderr, "malloc address: %p\n", dlsym(RTLD_NEXT, "malloc"));
    fprintf(stderr, "free address: %p\n", dlsym(RTLD_NEXT, "free"));
#endif

    return 0;
}
