// test_main.cpp — runs every registered unit test. Optional argv[1] is the
// layouts fixture directory; when given, JSON fixtures there are also run.
#include "test_harness.h"

#include <cstring>
#include <string>

int runLayoutFixtures(const std::string& dir);

int main(int argc, char** argv)
{
    if (argc > 1 && argv[1] && *argv[1])
        runLayoutFixtures(argv[1]);
    return cwtest::runAll();
}
