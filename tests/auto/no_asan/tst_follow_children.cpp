/*
    SPDX-FileCopyrightText: 2026 Méven Car <meven@kde.org>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "3rdparty/doctest.h"

#include "tempfile.h"
#include "tst_config.h"

#include <benchutil.h>

#include <dlfcn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using heaptrack_inject_t = void (*)(const char*);
using heaptrack_stop_t = void (*)();

namespace {
std::string readFile(const std::string& fileName)
{
    std::ifstream ifs(fileName, std::ios::binary);
    return {std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>()};
}

// a size that nothing else in this test allocates, to find in the data of a process
constexpr size_t childAllocationSize = 0x1092;
}

TEST_CASE ("follow forked children") {
    REQUIRE(setenv("HEAPTRACK_FOLLOW_CHILDREN", "1", 1) == 0);

    dlerror(); // clear error
    auto* handle = dlopen(HEAPTRACK_LIB_INJECT_SO, RTLD_NOW);
    if (!handle) {
        std::cerr << "DLOPEN FAILED: " << dlerror() << std::endl;
    }
    REQUIRE(handle);

    auto* heaptrack_inject = reinterpret_cast<heaptrack_inject_t>(dlsym(handle, "heaptrack_inject"));
    REQUIRE(heaptrack_inject);
    auto* heaptrack_stop = reinterpret_cast<heaptrack_stop_t>(dlsym(handle, "heaptrack_stop"));
    REQUIRE(heaptrack_stop);

    const TempFile base;
    heaptrack_inject((base.fileName + ".$$").c_str());

    // a thread that keeps allocating, so that the forks happen while it unwinds
    std::atomic<bool> stop {false};
    std::thread churn([&stop] {
        while (!stop) {
            auto* p = malloc(64);
            escape(p);
            free(p);
        }
    });

    std::vector<pid_t> children;
    for (int i = 0; i < 20; ++i) {
        const pid_t pid = fork();
        REQUIRE(pid != -1);
        if (pid == 0) {
            auto* p = malloc(childAllocationSize);
            escape(p);
            free(p);
            heaptrack_stop();
            _exit(0);
        }
        children.push_back(pid);
    }

    for (const auto pid : children) {
        int status = 0;
        REQUIRE(waitpid(pid, &status, 0) == pid);
        REQUIRE(WIFEXITED(status));
        REQUIRE(WEXITSTATUS(status) == 0);
    }

    stop = true;
    churn.join();
    heaptrack_stop();
    dlclose(handle);

    char hexSize[32];
    snprintf(hexSize, sizeof(hexSize), "\n+ %zx ", childAllocationSize);

    const auto parentFile = base.fileName + "." + std::to_string(getpid());
    const auto parent = readFile(parentFile);
    REQUIRE(!parent.empty());
    REQUIRE(parent.find(hexSize) == std::string::npos);
    boost::filesystem::remove(parentFile);

    for (const auto pid : children) {
        const auto childFile = base.fileName + "." + std::to_string(pid);
        const auto child = readFile(childFile);
        INFO("data of child " << pid);
        REQUIRE(!child.empty());
        // the child starts with the allocations of its parent, like an attached process
        REQUIRE(child.find("\nA\n") != std::string::npos);
        REQUIRE(child.find(hexSize) != std::string::npos);
        REQUIRE(child.find("\n-") != std::string::npos);
        boost::filesystem::remove(childFile);
    }
}
