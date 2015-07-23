#include "../trace.h"

#include <cassert>
#include <iostream>

using namespace std;

bool fill(Trace& trace, int depth, int skip)
{
    if (!depth) {
        return trace.fill(skip);
    } else {
        return fill(trace, depth - 1, skip);
    }
}

int main()
{
    Trace trace;
    assert(trace.size() == 0);

    assert(trace.fill(0));
    const auto offset = trace.size();
    assert(offset > 1);

    for (auto skip : {0, 1, 2}) {
        for (int i = 0; i < 2 * Trace::MAX_SIZE; ++i) {
            assert(fill(trace, i, skip));
            const auto expectedSize = min(i + offset + 1 - skip, static_cast<int>(Trace::MAX_SIZE) - skip);
            assert(trace.size() == expectedSize);
        }
    }

    return 0;
}
