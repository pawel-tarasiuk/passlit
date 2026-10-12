/* Internal counting and unranking checks. */
#include "../sampling.c"
static size_t allocation_size;
static int allocation_fail;

static void *api_malloc(size_t size) {
    allocation_size = size;
    return allocation_fail ? NULL : malloc(size);
}

#define malloc api_malloc
#include "../passlit.c"
#undef malloc

#include <stdio.h>
#include <assert.h>

static void exhaustive(const char *groups, const char *custom, size_t length,
        unsigned int required) {
    struct sampling_stats support;
    unsigned int families[ALPHABET_CAPACITY];
    char alphabet[ALPHABET_CAPACITY + 1];
    struct passlit_config config = { .count = 1, .workers = 1,
        .groups = groups, .custom = custom,
        .size_kind = PASSLIT_LENGTH, .size_val = 0
    };
    mpz_t rank;
    char result[8];
    size_t size = 0, possibilities = 1, expected = 0, i, c, rank_number;
    size_t value, position;
    unsigned char *seen;
    const char *found;
    unsigned int mask;
    int status;
    config.size_val = length;
    memset(&support, 0, sizeof(support));
    assert(parse_groups(&support, groups, custom) == PASSLIT_OK);
    for (c = 0; c < GROUP_COUNT; ++c) {
        for (i = 0; i < support.sizes[c]; ++i) {
            alphabet[size] = support.letters[c][i];
            families[size++] = 1U << c;
        }
    }
    alphabet[size] = '\0';
    for (i = 0; i < length; ++i) {
        possibilities *= size;
    }
    seen = calloc(possibilities, 1);
    assert(seen != NULL);
    /* Independent brute-force oracle. */
    for (i = 0; i < possibilities; ++i) {
        value = i;
        mask = 0;
        for (position = 0; position < length; ++position) {
            mask |= families[value % size];
            value /= size;
        }
        if ((mask & required) == required) {
            seen[i] = 1;
            ++expected;
        }
    }
    status = support_prepare(&support, &config);
    if (expected == 0) {
        assert(status == PASSLIT_RANGE);
        free(seen);
        return;
    }
    assert(status == PASSLIT_OK);
    assert(mpz_cmp_ui(support.counts[(length + 1) * support.states - 1],
            (unsigned long)(expected)) == 0);
    for (rank_number = 0; rank_number < expected; ++rank_number) {
        value = 0;
        mpz_init_set_ui(rank, (unsigned long)(rank_number));
        unrank_sample(result, &support, rank);
        assert(strlen(result) == length);
        for (i = 0; i < length; ++i) {
            found = strchr(alphabet, result[i]);
            assert(found != NULL);
            value = value * size + (size_t)(found - alphabet);
        }
        assert(seen[value] == 1);
        seen[value] = 2;
        mpz_clear(rank);
    }
    for (i = 0; i < possibilities; ++i) {
        assert(seen[i] != 1);
    }
    support_clear(&support);
    free(seen);
}

static void strength(const char *groups, size_t bits,
        enum passlit_size_kind size_kind, size_t length) {
    struct sampling_stats support;
    struct passlit_config config = { .count = 1, .workers = 1,
        .groups = groups, .custom = NULL,
        .size_kind = size_kind, .size_val = 0
    };
    config.size_val = bits;
    assert(support_prepare(&support, &config) == PASSLIT_OK);
    assert(support.length == length);
    support_clear(&support);
}

static void custom_checks(void) {
    struct sampling_stats support;
    char custom_pool[PASSLIT_CUSTOM_MAX_LENGTH + 2];
    struct passlit_config config = {
        .count = 1, .workers = 1, .groups = NULL, .size_val = 128U,
        .size_kind = PASSLIT_BITS_DOWN, .patterns = 0 };
    char output[32];
    mpz_t rank;
    size_t i;
    config.size_kind = PASSLIT_LENGTH;
    config.size_val = 16;
    memset(custom_pool, 'x', sizeof(custom_pool));
    custom_pool[PASSLIT_CUSTOM_MAX_LENGTH] = '\0';
    config.groups = "c";
    config.custom = custom_pool;
    assert(passlit_generate(output, sizeof(output), &config) ==
            PASSLIT_OK);
    assert(strlen(output) == 16);
    assert(strspn(output, "x") == 16);
    custom_pool[PASSLIT_CUSTOM_MAX_LENGTH] = 'x';
    custom_pool[PASSLIT_CUSTOM_MAX_LENGTH + 1] = '\0';
    assert(passlit_generate(output, sizeof(output), &config) ==
            PASSLIT_RANGE);
    assert(output[0] == '\0');
    config.groups = "U*c*";
    config.custom = "A!!A";
    config.size_val = 2;
    assert(support_prepare(&support, &config) == PASSLIT_OK);
    assert(strcmp(support.letters[GROUP_CUSTOM], "!") == 0);
    /* 16 uppercase choices for each of 2 positions, with 1 custom choice. */
    assert(mpz_cmp_ui(support.counts[3 * support.states - 1], 32) == 0);
    assert(support.states == 4);
    support_clear(&support);
    config.groups = "U*c";
    assert(support_prepare(&support, &config) == PASSLIT_OK);
    assert(mpz_cmp_ui(support.counts[3 * support.states - 1], 288) == 0);
    support_clear(&support);
    /* U overlap differs from u overlap. */
    config.groups = "cU";
    config.custom = "BBA!";
    assert(support_prepare(&support, &config) == PASSLIT_OK);
    assert(strcmp(support.letters[GROUP_CUSTOM], "!B") == 0);
    support_clear(&support);
    config.groups = "uc";
    assert(support_prepare(&support, &config) == PASSLIT_OK);
    assert(strcmp(support.letters[GROUP_CUSTOM], "!") == 0);
    support_clear(&support);
    config.groups = "uldoc";
    config.custom = "AB019!?";
    assert(support_prepare(&support, &config) == PASSLIT_OK);
    assert(support.sizes[GROUP_CUSTOM] == 0);
    assert(mpz_cmp_ui(support.counts[3 * support.states - 1], 8836) == 0);
    support_clear(&support);
    config.groups = "uldoc*";
    assert(support_prepare(&support, &config) == PASSLIT_RANGE);
    config.groups = "c";
    config.custom = NULL;
    assert(support_prepare(&support, &config) == PASSLIT_INVALID);
    config.custom = "";
    assert(support_prepare(&support, &config) == PASSLIT_INVALID);
    config.custom = "a b";
    assert(support_prepare(&support, &config) == PASSLIT_INVALID);
    config.custom = "\t";
    assert(support_prepare(&support, &config) == PASSLIT_INVALID);
    config.custom = "\177";
    assert(support_prepare(&support, &config) == PASSLIT_OK);
    support_clear(&support);
    config.custom = "\200";
    assert(support_prepare(&support, &config) == PASSLIT_OK);
    support_clear(&support);
    config.groups = NULL;
    config.custom = "abc";
    assert(support_prepare(&support, &config) == PASSLIT_INVALID);
    /* 5! times the product of pool sizes. */
    config.groups = "U*L*D*O*c*";
    config.custom = "0";
    config.size_val = 5;
    assert(support_prepare(&support, &config) == PASSLIT_OK);
    assert(support.states == 32);
    assert(mpz_cmp_ui(support.counts[6 * support.states - 1], 1459200) == 0);
    mpz_init(rank);
    for (i = 0; i < 2; ++i) {
        if (i == 0) {
            mpz_set_ui(rank, 0);
        } else {
            mpz_sub_ui(rank, support.counts[6 * support.states - 1], 1);
        }
        unrank_sample(output, &support, rank);
        assert(strlen(output) == 5);
        assert(strchr(output, '0') != NULL);
        assert(strpbrk(output, alphabets[GROUP_UPPERCASE][POOL_STANDARD]) !=
                NULL);
        assert(strpbrk(output, alphabets[GROUP_LOWERCASE][POOL_STANDARD]) !=
                NULL);
        assert(strpbrk(output, alphabets[GROUP_DIGITS][POOL_STANDARD]) !=
                NULL);
        assert(strpbrk(output, alphabets[GROUP_OTHERS][POOL_STANDARD]) !=
                NULL);
    }
    mpz_clear(rank);
    support_clear(&support);
    config.size_val = 4;
    assert(support_prepare(&support, &config) == PASSLIT_RANGE);
    assert(passlit_generate2(output, sizeof(output), "U*c*", "A!",
            PASSLIT_LENGTH, 2, NULL, 0,
            0, 1, 1) == PASSLIT_OK);
    assert(strchr(output, '!') != NULL);
    config.groups = "c*";
    config.custom = "xxxxx";
    config.size_val = 10;
    assert(passlit_generate(output, sizeof(output), &config) ==
            PASSLIT_OK);
    assert(strcmp(output, "xxxxxxxxxx") == 0);
    config.size_kind = PASSLIT_BITS_UP;
    config.size_val = 0;
    assert(passlit_generate(output, sizeof(output), &config) ==
            PASSLIT_OK);
    assert(strcmp(output, "x") == 0);
    config.size_val = 1;
    assert(support_prepare(&support, &config) == PASSLIT_RANGE);
    config.size_kind = PASSLIT_BITS_DOWN;
    config.size_val = 0;
    assert(support_prepare(&support, &config) == PASSLIT_RANGE);
    /* Exact bit boundary, both directions. */
    config.custom = "baba";
    config.size_val = 7;
    assert(support_prepare(&support, &config) == PASSLIT_OK);
    assert(support.length == 7);
    support_clear(&support);
    config.size_kind = PASSLIT_BITS_UP;
    assert(support_prepare(&support, &config) == PASSLIT_OK);
    assert(support.length == 7);
    support_clear(&support);
    /* D*c*, length 2: 10 samples, not 36. */
    config.groups = "D*c*";
    config.custom = "x";
    config.size_val = 4;
    assert(support_prepare(&support, &config) == PASSLIT_OK);
    assert(support.length == 3);
    support_clear(&support);
    config.size_kind = PASSLIT_BITS_DOWN;
    assert(support_prepare(&support, &config) == PASSLIT_OK);
    assert(support.length == 2);
    support_clear(&support);
}

static void allocating_api(void) {
    struct passlit_config config = PASSLIT_DEFAULT_CONFIG;
    char expected[128];
    char *out = NULL;
    int kind;

    config.seed = "allocation";
    for (kind = PASSLIT_LENGTH; kind <= PASSLIT_BITS_DOWN; ++kind) {
        config.size_kind = (enum passlit_size_kind)(kind);
        config.size_val = kind == PASSLIT_LENGTH ? 24 : 128;
        assert(passlit_generate(expected, sizeof(expected), &config) ==
                PASSLIT_OK);
        assert(passlit_generatep(&out, &config) == PASSLIT_OK);
        assert(out[strlen(expected) + 1] == '\0');
        assert(strcmp(out, expected) == 0);
        free(out);
        assert(passlit_generatep2(&out, config.groups, config.custom,
                config.size_kind, config.size_val, config.seed,
                config.sample_index, config.patterns, 1, 1) == PASSLIT_OK);
        assert(strcmp(out, expected) == 0);
        free(out);
    }
    out = expected;
    assert(passlit_generatep(&out, NULL) == PASSLIT_INVALID && out == NULL);
    assert(passlit_generatep(NULL, &config) == PASSLIT_INVALID);
    config.groups = "";
    assert(passlit_generatep(&out, &config) == PASSLIT_INVALID && out == NULL);
    config.groups = NULL;
    allocation_fail = 1;
    assert(passlit_generatep(&out, &config) == PASSLIT_MEMORY && out == NULL);
    allocation_fail = 0;
}

static void packed_checks(void) {
    enum { SAMPLE_LENGTH = 9, SAMPLE_COUNT = 4, THREADED_COUNT = 300 };
    enum { STRIDE = SAMPLE_LENGTH + 1 };
    struct passlit_config config = PASSLIT_DEFAULT_CONFIG;
    char buffer[SAMPLE_COUNT * STRIDE + 1], single[STRIDE + 1];
    char *output = NULL;
    size_t i;

    config.size_kind = PASSLIT_LENGTH;
    config.size_val = SAMPLE_LENGTH;
    config.seed = "packed";
    config.sample_index = 17;
    config.count = SAMPLE_COUNT;
    assert(passlit_generatep(&output, &config) == PASSLIT_OK);
    assert(passlit_generate(buffer, sizeof(buffer), &config) == PASSLIT_OK);
    assert(memcmp(buffer, output, sizeof(buffer)) == 0);
    assert(buffer[sizeof(buffer) - 1] == '\0');
    config.count = 1;
    for (i = 0; i < SAMPLE_COUNT; ++i) {
        assert(passlit_generate(single, sizeof(single), &config) ==
                PASSLIT_OK);
        assert(memcmp(single, output + i * STRIDE, STRIDE) == 0);
        ++config.sample_index;
    }
    free(output);
    config.count = SAMPLE_COUNT;
    assert(passlit_generate(buffer, sizeof(buffer) - 1, &config) ==
            PASSLIT_RANGE);
    assert(buffer[0] == '\0');
    config.count = 0;
    assert(passlit_generatep(&output, &config) == PASSLIT_OK);
    assert(allocation_size == 1 && *output == '\0');
    free(output);
    assert(passlit_generate(buffer, 1, &config) ==
            PASSLIT_OK && *buffer == '\0');
    config.count = THREADED_COUNT;
    config.workers = 4;
#ifdef PASSLIT_THREADS
    assert(passlit_generatep(&output, &config) == PASSLIT_OK);
    config.count = 1;
    config.workers = 1;
    for (i = 0; i < THREADED_COUNT; ++i) {
        assert(passlit_generate(single, sizeof(single), &config) ==
                PASSLIT_OK);
        assert(memcmp(single, output + i * STRIDE, STRIDE) == 0);
        ++config.sample_index;
    }
    assert(output[THREADED_COUNT * STRIDE] == '\0');
    free(output);
#else
    assert(passlit_generatep(&output, &config) == PASSLIT_INVALID);
    assert(output == NULL);
    config.workers = 1;
#endif
    config.count = SIZE_MAX;
    config.sample_index = 0;
    assert(passlit_generatep(&output, &config) == PASSLIT_RANGE);
    assert(output == NULL);
    config.count = 2;
    config.sample_index = UINT64_MAX;
    assert(passlit_generatep(&output, &config) == PASSLIT_RANGE);
    assert(output == NULL);
}

enum { CALLBACK_STOP = 93 };

struct batch_check {
    struct passlit_sampler *sampler;
    uint64_t offset;
    size_t seen, stop;
};

static int check_sample(const char *sample, void *context) {
    struct batch_check *check = context;
    char expected[32];
    assert(passlit_sampler_sample(expected, sizeof(expected), check->sampler,
            check->offset + check->seen) == PASSLIT_OK);
    assert(strcmp(sample, expected) == 0);
    ++check->seen;
    return check->seen == check->stop ? CALLBACK_STOP : PASSLIT_OK;
}

static void batch_checks(void) {
    struct passlit_config config = PASSLIT_DEFAULT_CONFIG;
    struct batch_check check;
    struct passlit_sampler *sampler = NULL;
    char seed[] = "batch", pool[] = "abcd";
    char expected[32], sample[32];
    size_t workers;

    config.groups = "c*";
    config.custom = pool;
    config.seed = seed;
    config.sample_index = 11;
    config.size_kind = PASSLIT_LENGTH;
    config.size_val = 12;
    assert(passlit_generate(expected, sizeof(expected), &config) ==
            PASSLIT_OK);
    assert(passlit_sampler_create(&sampler, &config) == PASSLIT_OK);
    config.sample_index += 7;
    assert(passlit_generate(sample, sizeof(sample), &config) == PASSLIT_OK);
    assert(passlit_sampler_sample(expected, sizeof(expected),
            sampler, 7) == PASSLIT_OK);
    assert(strcmp(sample, expected) == 0);
    config.sample_index -= 7;
    assert(passlit_generate(expected, sizeof(expected), &config) ==
            PASSLIT_OK);
    seed[0] = 'x';
    pool[0] = 'z';
    assert(passlit_sampler_length(sampler) == 12);
    assert(passlit_sampler_sample(sample, sizeof(sample), sampler, 0) ==
            PASSLIT_OK);
    assert(strcmp(sample, expected) == 0);
    check.sampler = sampler;
    check.offset = 7;
    for (workers = 1; workers <= 4; workers *= 4) {
        check.seen = check.stop = 0;
#ifndef PASSLIT_THREADS
        if (workers > 1) {
            assert(passlit_sampler_batch(sampler, 7, 300, workers,
                    check_sample, &check) == PASSLIT_INVALID);
            continue;
        }
#endif
        assert(passlit_sampler_batch(sampler, 7, 300, workers,
                check_sample, &check) == PASSLIT_OK);
        assert(check.seen == 300);
        check.seen = 0;
        check.stop = 19;
        assert(passlit_sampler_batch(sampler, 7, 300, workers,
                check_sample, &check) == CALLBACK_STOP);
        assert(check.seen == 19);
    }
    check.seen = 0;
    assert(passlit_sampler_batch(sampler, UINT64_MAX - 11, 2, 1,
            check_sample, &check) == PASSLIT_RANGE);
    assert(check.seen == 0);
    assert(passlit_sampler_batch(sampler, 0, 0, 1,
            check_sample, &check) == PASSLIT_INVALID);
    check.offset = 0;
    check.stop = 0;
    assert(passlit_sampler_batch(sampler, 0, 1, 0,
            check_sample, &check) == PASSLIT_OK);
    check.seen = 0;
    allocation_fail = 1;
    assert(passlit_sampler_batch(sampler, 0, 1, 1,
            check_sample, &check) == PASSLIT_MEMORY);
    allocation_fail = 0;
    passlit_sampler_free(sampler);
    passlit_sampler_free(NULL);
}

int main(void) {
    struct sampling_stats support;
    const char *invalid[] = {"",   "UU",  "Uu", "ll",  "dD",  "Oo",
                             "X",  "U L", "*U", "U**", "U*u", "U*U",
                             "cc", "c*c", "C",  "U *"};
    struct passlit_config config = {
        .count = 1, .workers = 1, .groups = NULL, .size_val = 128U,
        .size_kind = PASSLIT_BITS_DOWN, .patterns = 0 };
    mpz_t rank, count;
    char groups[11];
    size_t maximum = passlit_length_limit();
    size_t capacity = maximum + 2;
    size_t i, c, position;
    char *output = malloc(capacity);
    unsigned int mask;
    packed_checks();
    batch_checks();
    custom_checks();
    assert(output != NULL);
    assert(maximum >= PASSLIT_MIN_LENGTH_LIMIT);
    for (mask = 0; mask < 16; ++mask) {
        position = 0;
        for (c = 0; c < BUILTIN_GROUP_COUNT; ++c) {
            groups[position++] = "ULDO"[c];
            if ((mask & (1U << c)) != 0) {
                groups[position++] = '*';
            }
        }
        groups[position] = '\0';
        exhaustive(groups, NULL, 2, mask);
        groups[position++] = 'c';
        groups[position++] = '*';
        groups[position] = '\0';
        exhaustive(groups, "AAAz0z_", 2, mask | 16U);
    }
    exhaustive("D*O*", NULL, 3, 12U);
    exhaustive("uldo", NULL, 2, 0);
    exhaustive("c*", "baaba", 3, 16U);
    exhaustive("D*c*", "334xxy", 3, 20U);
    exhaustive("c*D*", "yxx433", 3, 20U);
    exhaustive("Uc", "AAA", 2, 0);
    strength("O", 6, PASSLIT_BITS_UP, 2);
    strength("O", 6, PASSLIT_BITS_DOWN, 2);
    strength("D", 7, PASSLIT_BITS_UP, 4);
    strength("D", 7, PASSLIT_BITS_DOWN, 3);
    strength(NULL, 128, PASSLIT_BITS_UP, 23);
    strength(NULL, 128, PASSLIT_BITS_DOWN, 22);
    strength("D*O*", 6, PASSLIT_BITS_UP, 2);
    strength("O", 3 * maximum, PASSLIT_BITS_DOWN, maximum);
    strength("O", 3 * maximum + 1, PASSLIT_BITS_DOWN, maximum);
    strength("O", 3 * maximum + 2, PASSLIT_BITS_DOWN, maximum);
    assert(passlit_generate2(output, capacity, "O", NULL, PASSLIT_BITS_DOWN,
            3 * maximum + 3, NULL, 0,
            0, 1, 1) == PASSLIT_RANGE);
    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        assert(passlit_generate2(output, capacity, invalid[i], NULL,
                PASSLIT_LENGTH, 8, NULL,
                0, 0, 1, 1) == PASSLIT_INVALID);
        assert(output[0] == '\0');
    }
    assert(passlit_generate2(NULL, 10, NULL, NULL, PASSLIT_LENGTH, 8, NULL,
            0, 0, 1, 1) == PASSLIT_INVALID);
    assert(passlit_generate2(output, 0, NULL, NULL, PASSLIT_LENGTH, 8, NULL,
            0, 0, 1, 1) == PASSLIT_INVALID);
    assert(passlit_generate(output, capacity, NULL) == PASSLIT_INVALID);
    assert(passlit_generate2(output, 8, NULL, NULL, PASSLIT_LENGTH, 8, NULL,
            0, 0, 1, 1) == PASSLIT_RANGE);
    assert(passlit_generate2(output, capacity, NULL, NULL, PASSLIT_LENGTH, 0,
            NULL, 0, 0, 1, 1) == PASSLIT_INVALID);
    assert(passlit_generate2(output, capacity, "U*L*D*O*", NULL,
            PASSLIT_LENGTH, 3, NULL, 0,
            0, 1, 1) == PASSLIT_RANGE);
    assert(passlit_generate2(output, capacity, "D", NULL, PASSLIT_BITS_DOWN, 0,
            NULL, 0, 0, 1, 1) == PASSLIT_RANGE);
    assert(passlit_generate2(output, capacity, "D", NULL, PASSLIT_BITS_UP, 0,
            NULL, 0, 0, 1, 1) == PASSLIT_OK);
    assert(strlen(output) == 1);
    assert(passlit_generate2(output, capacity, "D", NULL, PASSLIT_BITS_DOWN,
            7 * maximum, NULL, 0,
            0, 1, 1) == PASSLIT_RANGE);
    assert(passlit_generate2(output, capacity, NULL, NULL, PASSLIT_LENGTH,
            maximum + 1, NULL, 0,
            0, 1, 1) == PASSLIT_RANGE);
    assert(support_prepare(&support, &config) == PASSLIT_OK);
    assert(support.sizes[GROUP_UPPERCASE] == 16 &&
            support.sizes[GROUP_LOWERCASE] == 19 &&
            support.sizes[GROUP_DIGITS] == 5 &&
            support.sizes[GROUP_OTHERS] == 8);
    support_clear(&support);
    assert(support.states == 16);
    assert(passlit_generate(output, capacity, &config) == PASSLIT_OK);
    assert(strlen(output) == 22);
    config.size_kind = PASSLIT_LENGTH;
    config.groups = "o*l*D*U*";
    config.size_val = 4;
    for (i = 0; i < 100; ++i) {
        assert(passlit_generate(output, capacity, &config) ==
                PASSLIT_OK);
        assert(strlen(output) == 4);
        assert(strpbrk(output, alphabets[GROUP_UPPERCASE][POOL_STANDARD]) !=
                NULL);
        assert(strpbrk(output, alphabets[GROUP_LOWERCASE][POOL_ALL]) != NULL);
        assert(strpbrk(output, alphabets[GROUP_DIGITS][POOL_STANDARD]) !=
                NULL);
        assert(strpbrk(output, alphabets[GROUP_OTHERS][POOL_ALL]) != NULL);
    }
    mpz_inits(rank, count, NULL);
    for (i = 1; i <= 257; ++i) {
        mpz_set_ui(count, (unsigned long)(i));
        assert(random_rank(rank, count, NULL, 0) == PASSLIT_OK);
        assert(mpz_sgn(rank) >= 0 && mpz_cmp(rank, count) < 0);
    }
    mpz_clears(rank, count, NULL);
    free(output);
    allocating_api();
    puts("Exact counts, exhaustive rank bijections, boundaries, and API "
            "checks passed.");
    return 0;
}
