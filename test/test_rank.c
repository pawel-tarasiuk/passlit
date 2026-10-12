#include "../sampling.c"
#include "../passlit.c"

#include <stdio.h>
#include <assert.h>

static void batches(int patterns) {
    struct passlit_config config = PASSLIT_DEFAULT_CONFIG;
    mpz_t ranks[3], inverse[3], total;
    const char *samples[3];
    char *packed = NULL, *generated = NULL;
    char small[2] = "x";
    size_t length, i;

    config.patterns = patterns;
    config.count = 3;
    config.seed = "rank-test";
    mpz_init(total);
    for (i = 0; i < config.count; ++i) {
        mpz_inits(ranks[i], inverse[i], NULL);
    }
    assert(passlit_scope(total, &length, &config) == PASSLIT_OK);
    mpz_set_ui(ranks[0], 0);
    mpz_fdiv_q_ui(ranks[1], total, 2);
    mpz_sub_ui(ranks[2], total, 1);
    assert(passlit_encodep(&packed, ranks, &config) == PASSLIT_OK);
    for (i = 0; i < config.count; ++i) {
        samples[i] = packed + i * (length + 1);
    }
    assert(packed[config.count * (length + 1)] == '\0');
    assert(passlit_rank(inverse, samples, &config) == PASSLIT_OK);
    for (i = 0; i < config.count; ++i) {
        assert(mpz_cmp(inverse[i], ranks[i]) == 0);
    }
    free(packed);
    assert(passlit_generatep(&generated, &config) == PASSLIT_OK);
    for (i = 0; i < config.count; ++i) {
        samples[i] = generated + i * (length + 1);
    }
    assert(passlit_rank(ranks, samples, &config) == PASSLIT_OK);
    assert(passlit_encodep(&packed, ranks, &config) == PASSLIT_OK);
    assert(memcmp(packed, generated, config.count * (length + 1) + 1) == 0);
    free(packed);
    samples[1] = "";
    assert(passlit_rank(inverse, samples, &config) == PASSLIT_RANGE);
    assert(mpz_cmp_si(inverse[1], -1) == 0);
    assert(mpz_cmp(inverse[0], ranks[0]) == 0);
    assert(mpz_cmp(inverse[2], ranks[2]) == 0);
    samples[1] = NULL;
    assert(passlit_rank(inverse, samples, &config) == PASSLIT_RANGE);
    mpz_set(ranks[1], total);
    assert(passlit_encodep(&packed, ranks, &config) == PASSLIT_RANGE);
    assert(packed == NULL);
    mpz_set_si(ranks[1], -1);
    assert(passlit_encodep(&packed, ranks, &config) == PASSLIT_RANGE);
    assert(packed == NULL);
    assert(passlit_encode(small, sizeof(small), ranks, &config) ==
            PASSLIT_RANGE);
    assert(*small == '\0');
    config.groups = "";
    assert(passlit_rank(inverse, samples, &config) == PASSLIT_INVALID);
    for (i = 0; i < 3; ++i) {
        assert(mpz_cmp_si(inverse[i], -1) == 0);
        mpz_clears(ranks[i], inverse[i], NULL);
    }
    assert(passlit_scope(total, &length, &config) == PASSLIT_INVALID);
    assert(mpz_sgn(total) == 0 && length == 0);
    free(generated);
    mpz_clear(total);
}

static void exhaustive(void) {
    struct passlit_config config = PASSLIT_DEFAULT_CONFIG;
    mpz_t ranks[16], inverse[16], total;
    const char *samples[16];
    char *packed;
    size_t i, length;

    config.groups = "c";
    config.custom = "baab";
    config.patterns = 0;
    config.size_kind = PASSLIT_LENGTH;
    config.size_val = 4;
    config.count = 16;
    mpz_init(total);
    assert(passlit_scope(total, &length, &config) == PASSLIT_OK);
    assert(mpz_cmp_ui(total, 16) == 0 && length == 4);
    for (i = 0; i < config.count; ++i) {
        mpz_init_set_ui(ranks[i], (unsigned long)(i));
        mpz_init(inverse[i]);
    }
    assert(passlit_encodep(&packed, ranks, &config) == PASSLIT_OK);
    for (i = 0; i < config.count; ++i) {
        samples[i] = packed + i * (length + 1);
        assert(samples[i][0] == (i & 8 ? 'b' : 'a'));
        assert(samples[i][1] == (i & 4 ? 'b' : 'a'));
        assert(samples[i][2] == (i & 2 ? 'b' : 'a'));
        assert(samples[i][3] == (i & 1 ? 'b' : 'a'));
    }
    assert(passlit_rank(inverse, samples, &config) == PASSLIT_OK);
    for (i = 0; i < config.count; ++i) {
        assert(mpz_cmp(ranks[i], inverse[i]) == 0);
        mpz_clears(ranks[i], inverse[i], NULL);
    }
    free(packed);
    config.count = 0;
    assert(passlit_encodep(&packed, NULL, &config) == PASSLIT_OK);
    assert(*packed == '\0');
    assert(passlit_rank(NULL, NULL, &config) == PASSLIT_OK);
    free(packed);
    mpz_clear(total);
}

static void parallel_batches(int patterns) {
    enum { COUNT = 1025, LENGTH = 8, STRIDE = LENGTH + 1 };
    static const size_t counts[] = {255, 256, 257, COUNT};
    struct passlit_config config = PASSLIT_DEFAULT_CONFIG;
    mpz_t ranks[COUNT], serial[COUNT], parallel[COUNT];
    const char *samples[COUNT];
    char *expected, *actual;
    size_t i, n;

    config.groups = "c";
    config.custom = "abcd";
    config.size_kind = PASSLIT_LENGTH;
    config.size_val = LENGTH;
    config.patterns = patterns;
    for (i = 0; i < COUNT; ++i) {
        mpz_init_set_ui(ranks[i], (unsigned long)(i));
        mpz_inits(serial[i], parallel[i], NULL);
    }
    for (n = 0; n < sizeof(counts) / sizeof(*counts); ++n) {
        config.count = counts[n];
        config.workers = 1;
        assert(passlit_encodep(&expected, ranks, &config) == PASSLIT_OK);
        config.workers = 4;
#ifndef PASSLIT_THREADS
        assert(passlit_encodep(&actual, ranks, &config) == PASSLIT_INVALID);
        assert(actual == NULL);
        config.workers = 0;
#endif
        assert(passlit_encodep(&actual, ranks, &config) == PASSLIT_OK);
        assert(memcmp(expected, actual, config.count * STRIDE + 1) == 0);
        for (i = 0; i < config.count; ++i) {
            samples[i] = expected + i * STRIDE;
        }
        assert(passlit_rank(parallel, samples, &config) == PASSLIT_OK);
        for (i = 0; i < config.count; ++i) {
            assert(mpz_cmp(parallel[i], ranks[i]) == 0);
        }
        samples[127] = "invalid";
        samples[config.count - 1] = NULL;
        assert(passlit_rank(parallel, samples, &config) == PASSLIT_RANGE);
        config.workers = 1;
        assert(passlit_rank(serial, samples, &config) == PASSLIT_RANGE);
        for (i = 0; i < config.count; ++i) {
            assert(mpz_cmp(parallel[i], serial[i]) == 0);
        }
        free(expected);
        free(actual);
    }
    config.workers = 0;
    mpz_set_si(ranks[256], -1);
    assert(passlit_encodep(&actual, ranks, &config) == PASSLIT_RANGE);
    assert(actual == NULL);
    for (i = 0; i < COUNT; ++i) {
        mpz_clears(ranks[i], serial[i], parallel[i], NULL);
    }
}

static void allocated_ranks(void) {
    struct passlit_config config = PASSLIT_DEFAULT_CONFIG;
    const char *samples[] = {"aa", "invalid", "bb"};
    mpz_t *ranks = NULL;
    size_t i;

    config.groups = "c";
    config.custom = "ab";
    config.size_kind = PASSLIT_LENGTH;
    config.size_val = 2;
    config.count = 3;
    assert(passlit_rankp(&ranks, samples, &config) == PASSLIT_RANGE);
    assert(ranks != NULL);
    assert(mpz_cmp_ui(ranks[0], 0) == 0);
    assert(mpz_cmp_si(ranks[1], -1) == 0);
    assert(mpz_cmp_ui(ranks[2], 3) == 0);
    for (i = 0; i < config.count; ++i) {
        mpz_clear(ranks[i]);
    }
    free(ranks);
    assert(passlit_rankp(NULL, samples, &config) == PASSLIT_INVALID);
    assert(passlit_rankp(&ranks, samples, NULL) == PASSLIT_INVALID);
    assert(ranks == NULL);
    assert(passlit_rankp(&ranks, NULL, &config) == PASSLIT_INVALID);
    assert(ranks == NULL);
    config.groups = "";
    assert(passlit_rankp(&ranks, samples, &config) == PASSLIT_INVALID);
    assert(ranks == NULL);
    config.groups = "c";
    config.count = SIZE_MAX;
    assert(passlit_rankp(&ranks, samples, &config) == PASSLIT_RANGE);
    assert(ranks == NULL);
    config.count = 0;
    assert(passlit_rankp(&ranks, NULL, &config) == PASSLIT_OK);
    assert(ranks == NULL);
    config.groups = "U*D*";
    config.custom = NULL;
    config.size_val = 1;
    assert(passlit_rankp(&ranks, NULL, &config) == PASSLIT_RANGE);
    assert(ranks == NULL);
}

static void byte_pool(void) {
    struct passlit_config config = PASSLIT_DEFAULT_CONFIG;
    char pool[256], packed[3];
    const char *sample = packed;
    mpz_t ordinal, inverse, total;
    size_t i, count = 0, length;
    int policy;

    for (i = 1; i < 256; ++i) {
        if (strchr(" \t\n\r\v\f", (int)(i)) == NULL) {
            pool[count++] = (char)(i);
        }
    }
    pool[count] = '\0';
    config.groups = "c";
    config.custom = pool;
    config.size_kind = PASSLIT_LENGTH;
    config.size_val = 1;
    mpz_inits(ordinal, inverse, total, NULL);
    for (policy = 0; policy <= 1; ++policy) {
        config.patterns = policy;
        assert(passlit_scope(total, &length, &config) == PASSLIT_OK);
        assert(length == 1);
        assert(mpz_cmp_ui(total, 249) == 0);
        for (i = 0; i < count; ++i) {
            mpz_set_ui(ordinal, i);
            assert(passlit_encode(packed, sizeof(packed), &ordinal,
                    &config) == PASSLIT_OK);
            assert(packed[0] == pool[i]);
            assert(passlit_rank(&inverse, &sample, &config) == PASSLIT_OK);
            assert(mpz_cmp(ordinal, inverse) == 0);
        }
    }
    mpz_clears(ordinal, inverse, total, NULL);
}

int main(void) {
    byte_pool();
    batches(0);
    batches(1);
    exhaustive();
    allocated_ranks();
    parallel_batches(0);
    parallel_batches(1);
    puts("Rank batches, boundaries and inverses passed.");
    return 0;
}
