#include "platform.h"

#if !defined(PASSLIT_NO_PAGE_QUERY) || defined(PASSLIT_THREADS)
    #ifdef __unix__
        #include <unistd.h>
    #endif
#endif

size_t passlit_length_limit(void) {
    long page = -1;
#ifndef PASSLIT_NO_PAGE_QUERY
    #ifdef _SC_PAGESIZE
    page = sysconf(_SC_PAGESIZE);
    #elif defined(_SC_PAGE_SIZE)
    page = sysconf(_SC_PAGE_SIZE);
    #endif
#endif
    if (page < 4096) {
        page = 4096;
    }
    if (page > 65536) {
        page = 65536;
    }
    return (size_t)(page);
}


size_t passlit_worker_count(void) {
#if defined(PASSLIT_THREADS) && defined(_SC_NPROCESSORS_ONLN)
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpus >= 4) {
        return (size_t)(cpus / 2);
    }
#endif
    return 1;
}
