#include "test_skiparray.h"

/* Add all the definitions that need to be in the test runner's main file. */
GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();      /* command-line arguments, initialization. */
    RUN_SUITE(basic);
    RUN_SUITE(integration);
    RUN_SUITE(prop);
    GREATEST_MAIN_END();        /* display results */
}
