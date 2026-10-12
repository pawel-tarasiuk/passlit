#include "passlit.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

enum { SAMPLE_LENGTH = 8, SAMPLE_COUNT = 3, STRIDE = SAMPLE_LENGTH + 1 };

int main(void) {
    char packed[SAMPLE_COUNT * STRIDE + 1];
    char other[sizeof(packed)];
    struct passlit_config config = PASSLIT_DEFAULT_CONFIG;
    mpz_t ranks[SAMPLE_COUNT], total;
    mpz_t *allocated_ranks = NULL;
    const char *samples[SAMPLE_COUNT];
    char *allocated = NULL;
    size_t i, length;

    config = passlit_configure("c*", "abcd", PASSLIT_LENGTH,
            SAMPLE_LENGTH, "example", 17, -1, SAMPLE_COUNT, 2);
    assert(strcmp(config.groups, "c*") == 0);
    assert(strcmp(config.custom, "abcd") == 0);
    assert(strcmp(config.seed, "example") == 0);
    assert(config.size_kind == PASSLIT_LENGTH);
    assert(config.size_val == SAMPLE_LENGTH);
    assert(config.sample_index == 17);
    assert(config.patterns == -1);
    assert(config.count == SAMPLE_COUNT);
    assert(config.workers == 2);
    config.sample_index = 0;
    config.workers = 0;
    config.groups = "c*";
    config.custom = "abcd";
    config.seed = "example";
    config.size_kind = PASSLIT_LENGTH;
    config.size_val = SAMPLE_LENGTH;
    config.patterns = 0;
    config.count = SAMPLE_COUNT;

    assert(passlit_generate(packed, sizeof(packed), &config) == PASSLIT_OK);
    assert(strcmp(packed, "dcadcadc") == 0);
    assert(packed[sizeof(packed) - 1] == '\0');
    assert(passlit_generate2(other, sizeof(other), config.groups,
            config.custom, config.size_kind, config.size_val, config.seed,
            config.sample_index, config.patterns, config.count,
            config.workers) == PASSLIT_OK);
    assert(memcmp(packed, other, sizeof(packed)) == 0);
    assert(passlit_generatep(&allocated, &config) == PASSLIT_OK);
    assert(memcmp(packed, allocated, sizeof(packed)) == 0);
    free(allocated);
    assert(passlit_generatep2(&allocated, config.groups, config.custom,
            config.size_kind, config.size_val, config.seed,
            config.sample_index, config.patterns, config.count,
            config.workers) == PASSLIT_OK);
    assert(memcmp(packed, allocated, sizeof(packed)) == 0);
    free(allocated);

    mpz_init(total);
    assert(passlit_scope(total, &length, &config) == PASSLIT_OK);
    assert(length == SAMPLE_LENGTH);
    for (i = 0; i < SAMPLE_COUNT; ++i) {
        mpz_init(ranks[i]);
        samples[i] = packed + i * STRIDE;
    }
    assert(passlit_rank(ranks, samples, &config) == PASSLIT_OK);
    assert(passlit_encode(other, sizeof(other), ranks, &config) == PASSLIT_OK);
    assert(memcmp(packed, other, sizeof(packed)) == 0);
    assert(passlit_encodep(&allocated, ranks, &config) == PASSLIT_OK);
    assert(memcmp(packed, allocated, sizeof(packed)) == 0);
    free(allocated);
    assert(passlit_scope2(total, &length, config.groups, config.custom,
            config.size_kind, config.size_val, config.patterns) == PASSLIT_OK);
    assert(length == SAMPLE_LENGTH);
    assert(passlit_encode2(other, sizeof(other), ranks,
            config.groups, config.custom, config.size_kind, config.size_val,
            config.patterns, config.count, config.workers) == PASSLIT_OK);
    assert(memcmp(packed, other, sizeof(packed)) == 0);
    assert(passlit_encodep2(&allocated, ranks, config.groups, config.custom,
            config.size_kind, config.size_val, config.patterns,
            config.count, config.workers) == PASSLIT_OK);
    assert(memcmp(packed, allocated, sizeof(packed)) == 0);
    free(allocated);
    assert(passlit_rank2(ranks, samples, config.groups, config.custom,
            config.size_kind, config.size_val, config.patterns,
            config.count, config.workers) == PASSLIT_OK);
    assert(passlit_rankp(&allocated_ranks, samples, &config) == PASSLIT_OK);
    for (i = 0; i < SAMPLE_COUNT; ++i) {
        assert(mpz_cmp(allocated_ranks[i], ranks[i]) == 0);
        mpz_clear(allocated_ranks[i]);
    }
    free(allocated_ranks);
    assert(passlit_rankp2(&allocated_ranks, samples,
            config.groups, config.custom, config.size_kind, config.size_val,
            config.patterns, config.count, config.workers) == PASSLIT_OK);
    for (i = 0; i < SAMPLE_COUNT; ++i) {
        assert(mpz_cmp(allocated_ranks[i], ranks[i]) == 0);
        mpz_clear(allocated_ranks[i]);
    }
    free(allocated_ranks);
    for (i = 0; i < SAMPLE_COUNT; ++i) {
        mpz_clear(ranks[i]);
    }
    mpz_clear(total);

    assert(passlit_generate(other, sizeof(other) - 1, &config) ==
            PASSLIT_RANGE);
    assert(*other == '\0');
    config.count = 0;
    assert(passlit_generatep(&allocated, &config) == PASSLIT_OK);
    assert(*allocated == '\0');
    free(allocated);
    puts("Public header and library linkage checks passed.");
    return PASSLIT_OK;
}
