/*
 * Copyright 2014-2017 Milian Wolff <mail@milianw.de>
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

#include <future>
#include <thread>
#include <vector>

using namespace std;

const int ALLOCS_PER_THREAD = 1000;

int** alloc()
{
    int** block = new int*[ALLOCS_PER_THREAD];
    for (int i = 0; i < ALLOCS_PER_THREAD; ++i) {
        block[i] = new int;
    }
    return block;
}

void dealloc(future<int**>&& f)
{
    int** block = f.get();
    for (int i = 0; i < ALLOCS_PER_THREAD; ++i) {
        delete block[i];
    }
    delete[] block;
}

int main()
{
    vector<future<void>> futures;
    futures.reserve(100 * 4);
    for (int i = 0; i < 100; ++i) {
        auto f1 = async(launch::async, alloc);
        auto f2 = async(launch::async, alloc);
        auto f3 = async(launch::async, alloc);
        auto f4 = async(launch::async, alloc);
        futures.emplace_back(async(launch::async, dealloc, move(f1)));
        futures.emplace_back(async(launch::async, dealloc, move(f2)));
        futures.emplace_back(async(launch::async, dealloc, move(f3)));
        futures.emplace_back(async(launch::async, dealloc, move(f4)));
    }
    return 0;
}
