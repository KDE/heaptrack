/*
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

#include "3rdparty/catch.hpp"
#include "track/libheaptrack.h"
#include "util/linewriter.h"

#include <cstdio>
#include <cmath>

#include <thread>
#include <future>
#include <vector>
#include <iostream>

#include "tempfile.h"

bool initBeforeCalled = false;
bool initAfterCalled = false;
bool stopCalled = false;

using namespace std;

TEST_CASE ("api") {
    TempFile tmp; // opened/closed by heaptrack_init

    SECTION ("init") {
        heaptrack_init(tmp.fileName.c_str(),
                       []() {
                           REQUIRE(!initBeforeCalled);
                           REQUIRE(!initAfterCalled);
                           REQUIRE(!stopCalled);
                           initBeforeCalled = true;
                       },
                       [](LineWriter& out) {
                           REQUIRE(initBeforeCalled);
                           REQUIRE(!initAfterCalled);
                           REQUIRE(!stopCalled);
                           initAfterCalled = true;
                       },
                       []() {
                           REQUIRE(initBeforeCalled);
                           REQUIRE(initAfterCalled);
                           REQUIRE(!stopCalled);
                           stopCalled = true;
                       });

        REQUIRE(initBeforeCalled);
        REQUIRE(initAfterCalled);
        REQUIRE(!stopCalled);

        int data[2] = {0};

        SECTION ("no-op-malloc") {
            heaptrack_malloc(0, 0);
        }
        SECTION ("no-op-malloc-free") {
            heaptrack_free(0);
        }
        SECTION ("no-op-malloc-realloc") {
            heaptrack_realloc(data, 1, 0);
        }

        SECTION ("malloc-free") {
            heaptrack_malloc(data, 4);
            heaptrack_free(data);
        }

        SECTION ("realloc") {
            heaptrack_malloc(data, 4);
            heaptrack_realloc(data, 8, data);
            heaptrack_realloc(data, 16, data + 1);
            heaptrack_free(data + 1);
        }

        SECTION ("invalidate-cache") {
            heaptrack_invalidate_module_cache();
        }

        SECTION ("multi-threaded") {
            const auto numThreads = min(4u, thread::hardware_concurrency());

            cout << "start threads" << endl;
            {
                vector<future<void>> futures;
                for (unsigned i = 0; i < numThreads; ++i) {
                    futures.emplace_back(async(launch::async, [](){
                        for (int i = 0; i < 10000; ++i) {
                            heaptrack_malloc(&i, i);
                            heaptrack_realloc(&i, i + 1, &i);
                            heaptrack_free(&i);
                            if (i % 100 == 0) {
                                heaptrack_invalidate_module_cache();
                            }
                        }
                    }));
                }
            }
            cout << "threads finished" << endl;
        }

        SECTION ("stop") {
            heaptrack_stop();
            REQUIRE(stopCalled);
        }
    }
}
