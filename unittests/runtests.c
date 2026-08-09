/* Deprecated source-name compatibility wrapper.
 * Unit targets use test_main.c; this file deliberately contains no suite
 * registry so an older build script can still use the getter contract. */
#include "test_support.h"

int main(void)
{
    return ccl_run_suite(ccl_get_test_suite());
}
