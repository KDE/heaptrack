#include <thread>
#include <future>

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

bool dealloc(future<int**>&& f)
{
    int** block = f.get();
    for (int i = 0; i < ALLOCS_PER_THREAD; ++i) {
        delete block[i];
    }
    delete[] block;
    return true;
}

int main()
{
    for (int i = 0; i < 100; ++i) {
        auto f1 = async(launch::async, alloc);
        auto f2 = async(launch::async, alloc);
        auto f3 = async(launch::async, alloc);
        auto f4 = async(launch::async, alloc);
        auto f5 = async(launch::async, dealloc, move(f1));
        auto f6 = async(launch::async, dealloc, move(f2));
        auto f7 = async(launch::async, dealloc, move(f3));
        auto f8 = async(launch::async, dealloc, move(f4));
    }
    return 0;
}
