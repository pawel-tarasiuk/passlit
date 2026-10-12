/* Mock sysconf. */
#if !defined(PASSLIT_NO_PAGE_QUERY) && defined(__unix__)
#define sysconf test_sysconf
#endif
#include "../platform.c"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <limits.h>
#include <string.h>

#if !defined(PASSLIT_NO_PAGE_QUERY) &&                                        \
    (defined(_SC_PAGESIZE) || defined(_SC_PAGE_SIZE))
    #undef sysconf
static long simulated_page;
static unsigned int query_count;
long test_sysconf(int name) {
    (void)(name);
    ++query_count;
    return simulated_page;
}
#endif

int main(int argc, char **argv) {
#if !defined(PASSLIT_NO_PAGE_QUERY) &&                                        \
    (defined(_SC_PAGESIZE) || defined(_SC_PAGE_SIZE))
    size_t expected;
    if (argc <= 1) {
        simulated_page = -1;
    } else if (strcmp(argv[1], "max") == 0) {
        simulated_page = LONG_MAX;
    } else {
        simulated_page = strtol(argv[1], NULL, 10);
    }
    if (simulated_page < 4096) {
        expected = 4096;
    } else if (simulated_page > 65536) {
        expected = 65536;
    } else {
        expected = (size_t)(simulated_page);
    }
    assert(passlit_length_limit() == expected);
    assert(query_count == 1);
    assert(passlit_length_limit() == expected);
    assert(query_count == 2);
#if defined(PASSLIT_THREADS) && defined(_SC_NPROCESSORS_ONLN)
    for (simulated_page = -1; simulated_page <= 8; ++simulated_page) {
        expected = simulated_page >= 4 ? (size_t)(simulated_page / 2) : 1;
        assert(passlit_worker_count() == expected);
    }
#else
    assert(passlit_worker_count() == 1);
#endif
#else
    (void)(argc);
    (void)(argv);
    assert(passlit_length_limit() == 4096);
    assert(passlit_length_limit() == 4096);
#endif
    puts("Page-size and fallback checks passed.");
    return 0;
}
