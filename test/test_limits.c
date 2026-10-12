/* Simulated 64 KiB page. */
#include "../passlit.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>

static int fail_allocation;
static void *checked_malloc(size_t size) {
    if (fail_allocation) {
        return NULL;
    }
    return malloc(size);
}

#define sysconf boundary_sysconf
#define malloc checked_malloc
#include "../platform.c"
#include "../sampling.c"
#include "../passlit.c"
#undef malloc
#undef sysconf

long boundary_sysconf(int name) {
    (void)(name);
    return 65536;
}

int main(int argc, char **argv) {
    char pool[PASSLIT_CUSTOM_MAX_LENGTH + 2];
    struct passlit_config config = {
        .count = 1, .workers = 1, .groups = NULL, .size_val = 128U,
        .size_kind = PASSLIT_BITS_DOWN, .patterns = 0 };
    mpz_t ordinal, count;
    size_t maximum = passlit_length_limit();
    char *output = malloc(maximum + 2);
    size_t i;

    assert(output != NULL);
    assert(maximum == 65536);
    config.groups = "c*";
    config.custom = "x";
    config.size_kind = PASSLIT_LENGTH;
    config.size_val = maximum;
    assert(passlit_generate(output, maximum + 2, &config) ==
            PASSLIT_OK);
    assert(strlen(output) == maximum && strspn(output, "x") == maximum);
    assert(passlit_generate(output, maximum, &config) == PASSLIT_RANGE);
    assert(output[0] == '\0');
    config.size_val = maximum + 1;
    assert(passlit_generate(output, maximum + 2, &config) ==
            PASSLIT_RANGE);
    config.size_val = SIZE_MAX;
    assert(passlit_generate(output, maximum + 2, &config) ==
            PASSLIT_RANGE);
    config.size_kind = PASSLIT_BITS_UP;
    assert(passlit_generate(output, maximum + 2, &config) ==
            PASSLIT_RANGE);
    config.size_val = CHAR_BIT * maximum + 1;
    assert(passlit_generate(output, maximum + 2, &config) ==
            PASSLIT_RANGE);

    config.size_kind = PASSLIT_LENGTH;
    config.size_val =
        argc > 1 && strcmp(argv[1], "--full") == 0 ? maximum : 4096;
    config.groups = "uldo";
    config.custom = NULL;
    assert(passlit_generate(output, maximum + 2, &config) ==
            PASSLIT_OK);
    assert(strlen(output) == config.size_val);
    for (i = 0; i < config.size_val; ++i) {
        assert(output[i] >= '!' && output[i] <= '~');
    }
    config.groups = "U*L*D*O*c*";
    config.custom = "0";
    config.size_val = 512;
    assert(passlit_generate(output, maximum + 2, &config) ==
            PASSLIT_OK);
    assert(strlen(output) == config.size_val);
    assert(strchr(output, '0') != NULL);
    config.size_val = 4;
    assert(passlit_generate(output, maximum + 2, &config) ==
            PASSLIT_RANGE);

    config.groups = "c";
    config.custom = pool;
    config.size_val = 8;
    for (i = 0; i < PASSLIT_CUSTOM_MAX_LENGTH; ++i) {
        pool[i] = (char)('!' + i % ('~' - '!' + 1));
    }
    pool[PASSLIT_CUSTOM_MAX_LENGTH] = '\0';
    assert(passlit_generate(output, 10, &config) == PASSLIT_OK);
    assert(passlit_generate(output, 8, &config) == PASSLIT_RANGE);
    pool[PASSLIT_CUSTOM_MAX_LENGTH] = '!';
    pool[PASSLIT_CUSTOM_MAX_LENGTH + 1] = '\0';
    assert(passlit_generate(output, 10, &config) == PASSLIT_RANGE);
    pool[1] = '\0';
    for (i = 1; i < 256; ++i) {
        pool[0] = (char)(i);
        assert(passlit_generate(output, 10, &config) ==
                (!isspace((unsigned char)(i)) ? PASSLIT_OK : PASSLIT_INVALID));
    }

    config.custom = "xy";
    fail_allocation = 1;
    assert(passlit_generate(output, 10, &config) == PASSLIT_MEMORY);
    assert(output[0] == '\0');
    mpz_inits(ordinal, count, NULL);
    mpz_set_ui(count, 3);
    assert(random_rank(ordinal, count, NULL, 0) == PASSLIT_MEMORY);
    fail_allocation = 0;
    mpz_clears(ordinal, count, NULL);
    free(output);
    puts("Length, buffer, pool, arithmetic-boundary, and allocation-failure "
            "checks passed.");
    return 0;
}
