/*
 * Copyright 2020 Milian Wolff <mail@milianw.de>
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

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

#include <QVector>

#include "../../src/analyze/allocationdata.h"

constexpr uint64_t MAX_TREE_DEPTH = 64;
constexpr uint64_t NO_BRANCH_DEPTH = 4;
constexpr uint64_t BRANCH_WIDTH = 8;
constexpr uint64_t NUM_TRACES = 1000000;

using Trace = std::array<uint64_t, MAX_TREE_DEPTH>;

uint64_t generateIp(uint64_t level)
{
    if (level % NO_BRANCH_DEPTH) {
        return level;
    }
    static std::mt19937_64 engine(0);
    static std::uniform_int_distribution<uint64_t> dist(0, BRANCH_WIDTH - 1);
    return dist(engine);
}

Trace generateTrace()
{
    Trace trace;
    for (uint64_t i = 0; i < MAX_TREE_DEPTH; ++i) {
        trace[i] = generateIp(i);
    }
    return trace;
}

std::vector<Trace> generateTraces()
{
    std::vector<Trace> traces(NUM_TRACES);
    std::generate(traces.begin(), traces.end(), generateTrace);
    return traces;
}

namespace Vector {
template <template <typename...> class Container>
struct VectorTree
{
    AllocationData cost;
    uint64_t ip = 0;
    const VectorTree* parent = nullptr;
    Container<VectorTree> children;
};

template <template <typename...> class Container>
Container<VectorTree<Container>> buildTree(const std::vector<Trace>& traces)
{
    auto findNode = [](Container<VectorTree<Container>>* nodes, uint64_t ip) {
        auto it = std::find_if(nodes->begin(), nodes->end(),
                               [ip](const VectorTree<Container>& node) { return node.ip == ip; });
        if (it != nodes->end())
            return it;
        return nodes->insert(it, VectorTree<Container> {{}, ip, nullptr, {}});
    };

    Container<VectorTree<Container>> ret;
    for (const auto& trace : traces) {
        auto* nodes = &ret;
        for (const auto& ip : trace) {
            auto it = findNode(nodes, ip);
            it->cost.allocations++;
            nodes = &it->children;
        }
    }
    return ret;
}

template <template <typename...> class Container>
uint64_t numNodes(const VectorTree<Container>& node)
{
    return std::accumulate(node.children.begin(), node.children.end(), uint64_t(1),
                           [](uint64_t count, const VectorTree<Container>& node) { return count + numNodes(node); });
}

template <template <typename...> class Container>
std::pair<uint64_t, uint64_t> run(const std::vector<Trace>& traces)
{
    const auto tree = buildTree<Container>(traces);
    const auto totalNodes =
        std::accumulate(tree.begin(), tree.end(), uint64_t(0),
                        [](uint64_t count, const VectorTree<Container>& node) { return count + numNodes(node); });
    return {tree.size(), totalNodes};
}
}

namespace QtTree {
std::pair<uint64_t, uint64_t> run(const std::vector<Trace>& traces)
{
    return Vector::run<QVector>(traces);
}
}

namespace StdTree {
std::pair<uint64_t, uint64_t> run(const std::vector<Trace>& traces)
{
    return Vector::run<std::vector>(traces);
}
}

enum class Tag
{
    Qt,
    Std
};

std::pair<uint64_t, uint64_t> run(const std::vector<Trace>& traces, Tag tag)
{
    switch (tag) {
    case Tag::Qt:
        return QtTree::run(traces);
    case Tag::Std:
        return StdTree::run(traces);
    }
    Q_UNREACHABLE();
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: bench_tree [qt|std]\n";
        return 1;
    }

    const auto tag = [&]() {
        auto t = std::string(argv[1]);
        if (t == "qt")
            return Tag::Qt;
        if (t == "std")
            return Tag::Std;
        std::cerr << "unhandled tag: " << t << "\n";
        exit(1);
    }();

    const auto traces = generateTraces();
    const auto result = run(traces, tag);
    std::cout << result.first << ", " << result.second << std::endl;
    return 0;
}
