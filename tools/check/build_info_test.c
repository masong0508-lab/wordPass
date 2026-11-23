// Native regression for the two on-screen ROM identities; no DS SDK needed.
#include "build_info.h"
#include <assert.h>
#include <string.h>

_Static_assert(sizeof(WP_BUILD_LABEL) - 1 <= 16,
               "Build label must end before the title pager at x=118");
_Static_assert(sizeof(WP_BUILD_FLAVOR) - 1 <= 8,
               "Flavor label must fit within the top title border");

int main(void)
{
#ifdef BUILD_INFO_TEST_OVERRIDE
    assert(strcmp(WP_BUILD_LABEL, "BUILD 123abcd") == 0);
    assert(strcmp(WP_BUILD_FLAVOR, "CYR DEMO") == 0);
#else
    assert(strcmp(WP_BUILD_LABEL, "BUILD source") == 0);
    assert(strcmp(WP_BUILD_FLAVOR, "RETAIL") == 0);
#endif
    return 0;
}
