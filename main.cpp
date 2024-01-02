#include <iostream>
#include "test.h"

int main (int agrc, char* argv[])
{

#ifdef BUILD_TEST
    Test::getInstance()->FnTest(argv[0]);
    return 0;
#endif

    return 0;
}