#include <iostream>
#include <unistd.h>

using namespace std;

int main(int argc, char **argv)
{
    cerr << "This is just a test utility. To use this debug utility, run your app like this:" << endl
         << endl
         << "  DUMP_MALLOC_INFO_INTERVAL=100 LD_PRELOAD=./path/to/libdumpmallocinfo.cpp yourapp" << endl
         << endl
         << "The above will output the XML malloc info every 100ms." << endl;

    srand(0);
    for(int i = 0; i < 10000; ++i) {
        delete new int;
        new int[rand() % 100];
        malloc(rand() % 1000);
    }
    return 0;
}
