/*
    SPDX-FileCopyrightText: 2018 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "3rdparty/doctest.h"

#include "tempfile.h"
#include "tst_config.h"

#include <benchutil.h>

#include <dlfcn.h>

#include <iostream>

static_assert(RTLD_NOW == 0x2, "RTLD_NOW needs to equal 0x2");

using heaptrack_inject_t = void (*)(const char*);
using heaptrack_stop_t = void (*)();

namespace {
template <typename T>
T resolveSymbol(void* handle, const char* symbol)
{
    return reinterpret_cast<T>(dlsym(handle, symbol));
}

heaptrack_inject_t resolveHeaptrackInject(void* handle)
{
    return resolveSymbol<heaptrack_inject_t>(handle, "heaptrack_inject");
}

heaptrack_stop_t resolveHeaptrackStop(void* handle)
{
    return resolveSymbol<heaptrack_stop_t>(handle, "heaptrack_stop");
}

template <typename Load, typename Unload>
void runInjectTest(Load load, Unload unload)
{
    REQUIRE(!resolveHeaptrackInject(RTLD_DEFAULT));
    REQUIRE(!resolveHeaptrackStop(RTLD_DEFAULT));

    auto* handle = load();
    REQUIRE(handle);

    auto* heaptrack_inject = resolveHeaptrackInject(handle);
    REQUIRE(heaptrack_inject);

    auto* heaptrack_stop = resolveHeaptrackStop(handle);
    REQUIRE(heaptrack_stop);

    TempFile file;

    heaptrack_inject(file.fileName.c_str());

    auto* p = malloc(100);
    escape(p);
    free(p);

    heaptrack_stop();

    unload(handle);

    REQUIRE(!resolveHeaptrackInject(RTLD_DEFAULT));
    REQUIRE(!resolveHeaptrackStop(RTLD_DEFAULT));

    const auto contents = file.readContents();
    REQUIRE(!contents.empty());
    REQUIRE(contents.find("\nA\n") != std::string::npos);
    REQUIRE(contents.find("\n+") != std::string::npos);
    REQUIRE(contents.find("\n-") != std::string::npos);
}
}

TEST_CASE ("inject via dlopen") {
    runInjectTest(
        []() -> void* {
            dlerror(); // clear error
            auto* handle = dlopen(HEAPTRACK_LIB_INJECT_SO, RTLD_NOW);
            if (!handle) {
                std::cerr << "DLOPEN FAILED: " << dlerror() << std::endl;
            }
            return handle;
        },
        [](void* handle) { dlclose(handle); });
}

#ifdef __USE_GNU
TEST_CASE ("inject via dlmopen") {
    runInjectTest(
        []() -> void* {
            dlerror(); // clear error
            auto* handle = dlmopen(LM_ID_BASE, HEAPTRACK_LIB_INJECT_SO, RTLD_NOW);
            if (!handle) {
                std::cerr << "DLMOPEN FAILED: " << dlerror() << std::endl;
            }
            return handle;
        },
        [](void* handle) { dlclose(handle); });
}
#endif

extern "C" {
__attribute__((weak)) void* __libc_dlopen_mode(const char* filename, int flag);
__attribute__((weak)) int __libc_dlclose(void* handle);
}

TEST_CASE ("inject via libc") {
    if (!__libc_dlopen_mode) {
        INFO("__libc_dlopen_mode symbol not available");
        return;
    }

    runInjectTest([]() { return __libc_dlopen_mode(HEAPTRACK_LIB_INJECT_SO, 0x80000000 | 0x002); },
                  [](void* handle) { __libc_dlclose(handle); });
}
