#include "../sampling.c"
#include "../passlit.c"

#include <stdio.h>
#include <assert.h>

static int allowed(const char *word, const unsigned int *groups,
        size_t length, size_t group_count, unsigned int required) {
    size_t i, j;
    unsigned int seen = 0;
    for (i = 0; i < length; ++i) {
        seen |= 1U << groups[i];
        if (i >= 3 && word[i] == word[i - 1] &&
                word[i] == word[i - 2] && word[i] == word[i - 3]) {
            return 0;
        }
        if (group_count > 1 && i >= 4 && groups[i] == groups[i - 1] &&
                groups[i] == groups[i - 2] && groups[i] == groups[i - 3] &&
                groups[i] == groups[i - 4]) {
            return 0;
        }
        for (j = 2; j <= 3; ++j) {
            if (i + 1 >= 2 * j &&
                    memcmp(word + i + 1 - 2 * j, word + i + 1 - j, j) == 0) {
                return 0;
            }
        }
    }
    return (seen & required) == required;
}

static void exhaustive(const char *description, const char *custom,
        size_t length) {
    struct sampling_stats support;
    struct passlit_config config = PASSLIT_DEFAULT_CONFIG;
    unsigned int families[ALPHABET_CAPACITY], groups[10], required = 0;
    char alphabet[ALPHABET_CAPACITY + 1], word[11], sample[11];
    mpz_t ordinal, inverse;
    size_t size = 0, group_count = 0, possible = 1, expected = 0;
    size_t c, i, value, code;
    config.groups = description;
    config.custom = custom;
    config.size_kind = PASSLIT_LENGTH;
    config.size_val = length;
    assert(support_prepare(&support, &config) == PASSLIT_OK);
    assert(support.automaton_count < PATTERN_MAX_STATES);
    for (c = 0; c < GROUP_COUNT; ++c) {
        group_count += support.sizes[c] != 0 ? 1U : 0U;
        required |= support.needs[c] != 0 ? 1U << c : 0;
        for (i = 0; i < support.sizes[c]; ++i) {
            alphabet[size] = support.letters[c][i];
            families[size++] = (unsigned int)(c);
        }
    }
    for (i = 0; i < length; ++i) {
        possible *= size;
    }
    mpz_inits(ordinal, inverse, NULL);
    word[length] = '\0';
    for (code = 0; code < possible; ++code) {
        value = code;
        for (i = length; i != 0; --i) {
            word[i - 1] = alphabet[value % size];
            groups[i - 1] = families[value % size];
            value /= size;
        }
        if (!allowed(word, groups, length, group_count, required)) {
            assert(sampling_rank(inverse, &support, word) == PASSLIT_RANGE);
            assert(mpz_cmp_si(inverse, -1) == 0);
            continue;
        }
        mpz_set_ui(ordinal, (unsigned long)(expected++));
        unrank_sample(sample, &support, ordinal);
        assert(strcmp(sample, word) == 0);
        assert(sampling_rank(inverse, &support, word) == PASSLIT_OK);
        assert(mpz_cmp_ui(inverse, (unsigned long)(expected - 1)) == 0);
    }
    assert(mpz_cmp_ui(support.pattern_rows[length][support.states - 1],
            (unsigned long)(expected)) == 0);
    mpz_clears(ordinal, inverse, NULL);
    support_clear(&support);
}

static void defaults(void) {
    struct passlit_config config = PASSLIT_DEFAULT_CONFIG;
    struct sampling_stats support;
    mpz_t target;
    mpz_srcptr count;
    char expected[32], sample[32];
    size_t length;
    int policy, up;
    config.seed = "default";
    mpz_init_set_ui(target, 1);
    mpz_mul_2exp(target, target, 128);
    for (policy = 0; policy <= 1;
            ++policy) {
        config.patterns = policy;
        for (up = 0; up <= 1; ++up) {
            length = up ? 23 : 22;
            config.groups = NULL;
            config.custom = NULL;
            config.size_kind = PASSLIT_LENGTH;
            config.size_val = length;
            assert(support_prepare(&support, &config) == PASSLIT_OK);
            count = support.pattern_rows != NULL ?
                support.pattern_rows[length][support.states - 1] :
                support.counts[(length + 1) * support.states - 1];
            assert(up ? mpz_cmp(count, target) > 0 :
                    mpz_cmp(count, target) <= 0);
            support_clear(&support);
            assert(passlit_generate(expected, sizeof(expected),
                    &config) == PASSLIT_OK);
            config.size_kind = up ? PASSLIT_BITS_UP : PASSLIT_BITS_DOWN;
            config.size_val = 128;
            assert(passlit_generate(sample, sizeof(sample), &config)
                    == PASSLIT_OK);
            assert(strcmp(sample, expected) == 0);
            config.groups = "cO*D*L*U*";
            config.custom = "AA33";
            assert(passlit_generate(sample, sizeof(sample), &config)
                    == PASSLIT_OK);
            assert(strcmp(sample, expected) == 0);
        }
    }
    memset(&support, 0, sizeof(support));
    config.groups = "U*L*D*O";
    config.custom = NULL;
    assert(parse_groups(&support, config.groups, NULL) == PASSLIT_OK);
    assert(default_length(&support, &config) == 0);
    mpz_clear(target);
}

int main(void) {
    struct sampling_stats support, exact;
    struct passlit_config config = PASSLIT_DEFAULT_CONFIG;
    mpz_t target;
    char output[32], repeat[32];
    size_t length, bits;
    defaults();
    for (length = 1; length <= 9; ++length) {
        exhaustive("c", "ab", length);
        exhaustive("c*", "abc", length);
    }
    exhaustive("D*c*", "a", 5);
    exhaustive("c", "abcd", 9);
    exhaustive("D*c*", "a", 6);
    exhaustive("Dc", "ab", 6);
    exhaustive("D*O*c*", "a", 4);
    mpz_init(target);
    config.groups = "c*";
    config.custom = "ab";
    for (bits = 1; bits <= 12; ++bits) {
        mpz_set_ui(target, 1);
        mpz_mul_2exp(target, target, (mp_bitcnt_t)(bits));
        config.size_val = bits;
        config.size_kind = PASSLIT_BITS_DOWN;
        assert(support_prepare(&support, &config) == PASSLIT_OK);
        assert(
                mpz_cmp(support.pattern_rows[support.length]
                [support.states - 1],
                target) <= 0);
        config.size_val = support.length + 1;
        config.size_kind = PASSLIT_LENGTH;
        assert(support_prepare(&exact, &config) == PASSLIT_OK);
        assert(mpz_cmp(exact.pattern_rows[exact.length][exact.states - 1],
                target) > 0);
        support_clear(&exact);
        support_clear(&support);
        config.size_val = bits;
        config.size_kind = PASSLIT_BITS_UP;
        assert(support_prepare(&support, &config) == PASSLIT_OK);
        assert(
                mpz_cmp(support.pattern_rows[support.length]
                [support.states - 1],
                target) >= 0);
        support_clear(&support);
    }
    mpz_clear(target);
    config.size_kind = PASSLIT_LENGTH;
    config.size_val = 24;
    config.seed = "patterns";
    assert(passlit_generate(output, sizeof(output), &config) == PASSLIT_OK);
    assert(passlit_generate2(repeat, sizeof(repeat), config.groups,
            config.custom, config.size_kind, config.size_val, config.seed,
            config.sample_index, config.patterns, 1, 1) == PASSLIT_OK);
    assert(strcmp(output, repeat) == 0);
    config.groups = "U*L*D*O*c*";
    config.custom = "0";
    config.size_val = 5;
    assert(support_prepare(&support, &config) == PASSLIT_OK);
    assert(support.automaton_count < PATTERN_MAX_STATES);
    support_clear(&support);
    assert(passlit_generate(output, sizeof(output), &config) == PASSLIT_OK);
    assert(strchr(output, '0') != NULL);
    config.patterns = 2;
    assert(passlit_generate(repeat, sizeof(repeat), &config) == PASSLIT_OK);
    assert(strcmp(output, repeat) == 0);
    config.patterns = -1;
    assert(passlit_generate(repeat, sizeof(repeat), &config) == PASSLIT_OK);
    assert(strcmp(output, repeat) == 0);
    puts("Pattern counts, unranking and bit rounding passed.");
    return 0;
}
