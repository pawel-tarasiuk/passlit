/* Licensed under the GNU Lesser General Public License, version 3 only. */

#include "sampling.h"
#include "platform.h"

#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include <string.h>

#include <gmp.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

enum {
    ALPHABET_CAPACITY = UCHAR_MAX,
    DEFAULT_LENGTH_DOWN = 22,
    DEFAULT_LENGTH_UP = 23
};

enum group_index {
    GROUP_UPPERCASE,
    GROUP_LOWERCASE,
    GROUP_DIGITS,
    GROUP_OTHERS,
    GROUP_CUSTOM,
    GROUP_COUNT,
    BUILTIN_GROUP_COUNT = GROUP_CUSTOM
};

enum pool_variant {
    POOL_STANDARD,
    POOL_ALL,
    POOL_VARIANT_COUNT
};

enum pool_membership {
    POOL_ABSENT,
    POOL_BUILTIN,
    POOL_CUSTOM
};

static const char *const alphabets[BUILTIN_GROUP_COUNT][POOL_VARIANT_COUNT] = {
    { "ACEFHKLMNPRTVXYZ", "ABCDEFGHIJKLMNOPQRSTUVWXYZ" },
    { "abcdefghkmnpqstvxyz", "abcdefghijklmnopqrstuvwxyz" },
    { "34679", "0123456789" },
    { "_-.^+=?*", "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~" }
};

struct sampling_stats {
    char custom[PASSLIT_CUSTOM_MAX_LENGTH + 1];
    const char *letters[GROUP_COUNT];
    size_t sizes[GROUP_COUNT];
    mpz_t *counts;
    mpz_t **pattern_rows;
    struct pattern_state *automaton;
    size_t automaton_count;
    size_t states;
    size_t length;
    size_t rows;
    unsigned int needs[GROUP_COUNT];
};

static void support_clear(struct sampling_stats *support) {
    size_t i, n;
    if (support->pattern_rows != NULL) {
        for (n = 0; n < support->rows; ++n) {
            for (i = 0; i < support->states * support->automaton_count; ++i) {
                mpz_clear(support->pattern_rows[n][i]);
            }
            free(support->pattern_rows[n]);
        }
        free(support->pattern_rows);
        free(support->automaton);
        return;
    }
    free(support->automaton);
    for (i = 0; i < support->rows * support->states; ++i) {
        mpz_clear(support->counts[i]);
    }
    free(support->counts);
}

static int parse_groups(struct sampling_stats *support,
        const char *description, const char *custom) {
    unsigned char present[ALPHABET_CAPACITY + 1] = { POOL_ABSENT };
    static const char selectors[] = "UuLlDdOo";
    const char *p = description != NULL ? description : "U*L*D*O*";
    const char *found;
    size_t c, j, index, raw_length, total = 0;
    unsigned int bit, selected = 0, required = 0;
    const unsigned int custom_bit = 1U << GROUP_CUSTOM;
    unsigned char value;
    if (*p == '\0') {
        return PASSLIT_INVALID;
    }
    while (*p != '\0') {
        if (*p == 'c') {
            bit = custom_bit;
        } else {
            found = strchr(selectors, *p);
            if (found == NULL) {
                return PASSLIT_INVALID;
            }
            index = (size_t)(found - selectors);
            bit = 1U << (index >> 1);
            support->letters[index >> 1] = alphabets[index >> 1][index & 1U];
            support->sizes[index >> 1] = strlen(support->letters[index >> 1]);
        }
        if ((selected & bit) != 0) {
            return PASSLIT_INVALID;
        }
        selected |= bit;
        ++p;
        if (*p == '*') {
            required |= bit;
            ++p;
        }
    }
    if ((selected & custom_bit) == 0 && custom != NULL) {
        return PASSLIT_INVALID;
    }
    for (c = 0; c < BUILTIN_GROUP_COUNT; ++c) {
        for (j = 0; j < support->sizes[c]; ++j) {
            present[(unsigned char)(support->letters[c][j])] = POOL_BUILTIN;
        }
    }
    if ((selected & custom_bit) != 0) {
        if (custom == NULL || *custom == '\0') {
            return PASSLIT_INVALID;
        }
        /* Built-ins win; canonicalize the residual pool. */
        for (p = custom, raw_length = 0; *p != '\0'; ++p, ++raw_length) {
            if (raw_length == PASSLIT_CUSTOM_MAX_LENGTH) {
                return PASSLIT_RANGE;
            }
            value = (unsigned char)(*p);
            if (isspace(value)) {
                return PASSLIT_INVALID;
            }
            if (present[value] == POOL_ABSENT) {
                present[value] = POOL_CUSTOM;
            }
        }
        for (c = 1; c <= ALPHABET_CAPACITY; ++c) {
            if (present[c] == POOL_CUSTOM) {
                support->custom[support->sizes[GROUP_CUSTOM]++] = (char)(c);
            }
        }
        support->custom[support->sizes[GROUP_CUSTOM]] = '\0';
        support->letters[GROUP_CUSTOM] = support->custom;
    }
    /* Only required groups consume mask bits. */
    support->states = 1;
    for (c = 0; c < GROUP_COUNT; ++c) {
        total += support->sizes[c];
        if ((required & (1U << c)) != 0) {
            if (support->sizes[c] == 0) {
                return PASSLIT_RANGE;
            }
            support->needs[c] = (unsigned int)(support->states);
            support->states *= 2;
        }
    }
    return total != 0 ? PASSLIT_OK : PASSLIT_RANGE;
}

enum {
    PATTERN_HISTORY = 5,
    PATTERN_SYMBOLS = 3,
    PATTERN_CHOICES = PATTERN_SYMBOLS + GROUP_COUNT,
    PATTERN_GROUP_RUN = 4,
    /* Colored partitions of up to three distinct suffix symbols. */
    PATTERN_MAX_STATES = 1 + PATTERN_GROUP_RUN *
        (GROUP_COUNT + GROUP_COUNT * (GROUP_COUNT + 1) +
                3 * (GROUP_COUNT + 3 * GROUP_COUNT * GROUP_COUNT +
                GROUP_COUNT * GROUP_COUNT * GROUP_COUNT)),
    PATTERN_HASH_SLOTS = 8192
};

struct pattern_state {
    size_t next[PATTERN_CHOICES];
    unsigned char weight[PATTERN_CHOICES];
    unsigned char group[PATTERN_CHOICES];
    unsigned char symbols[PATTERN_HISTORY];
    unsigned char groups[PATTERN_HISTORY];
    unsigned char length, run;
};

static uint32_t pattern_key(const struct pattern_state *state) {
    uint32_t key = state->run;
    size_t i;
    key = (key << 3) | state->length;
    for (i = 0; i < state->length; ++i) {
        key = (key << 4) |
            (uint32_t)(state->groups[i] * PATTERN_SYMBOLS +
                    state->symbols[i] + 1);
    }
    return key;
}

static int pattern_step(struct pattern_state *next,
        const struct pattern_state *state,
        unsigned char symbol, unsigned char group,
        size_t group_count) {
    unsigned char symbols[PATTERN_HISTORY + 1];
    unsigned char groups[PATTERN_HISTORY + 1];
    unsigned char labels[PATTERN_SYMBOLS + 1];
    size_t length = state->length, keep, i, distinct = 0;
    unsigned int run;
    memcpy(symbols, state->symbols, length);
    memcpy(groups, state->groups, length);
    symbols[length] = symbol;
    groups[length] = group;
    ++length;
    run = length > 1 && group == groups[length - 2] ? state->run + 1U : 1U;
    if (group_count > 1 && run > PATTERN_GROUP_RUN) {
        return 0;
    }
    if (length >= 4 && symbols[length - 4] == symbols[length - 2] &&
            symbols[length - 3] == symbols[length - 1]) {
        return 0;
    }
    if (length >= 6 && symbols[0] == symbols[3] &&
            symbols[1] == symbols[4] && symbols[2] == symbols[5]) {
        return 0;
    }
    /* Four identical symbols are also an adjacent repeated pair. */
    keep = length < PATTERN_SYMBOLS ? length : PATTERN_SYMBOLS;
    if (length >= 4 && symbols[length - 4] == symbols[length - 1]) {
        keep = 4;
    }
    if (length >= 5 && symbols[length - 5] == symbols[length - 2] &&
            symbols[length - 4] == symbols[length - 1]) {
        keep = PATTERN_HISTORY;
    }
    memset(next, 0, sizeof(*next));
    memset(labels, PATTERN_SYMBOLS + 1, sizeof(labels));
    next->length = (unsigned char)(keep);
    next->run = group_count > 1 ? (unsigned char)(run) : 1;
    for (i = 0; i < keep; ++i) {
        symbol = symbols[length - keep + i];
        if (labels[symbol] == PATTERN_SYMBOLS + 1) {
            labels[symbol] = (unsigned char)(distinct++);
        }
        next->symbols[i] = labels[symbol];
        next->groups[i] = groups[length - keep + i];
    }
    return 1;
}

static int pattern_graph(struct sampling_stats *support) {
    struct pattern_state candidate;
    struct pattern_state *state;
    size_t slots[PATTERN_HASH_SLOTS] = { 0 };
    size_t used[GROUP_COUNT], group_count = 0;
    size_t i, j, c, choice, distinct, weight, slot, index;
    uint32_t key, hash;
    unsigned char symbol, group;
    support->automaton = calloc(PATTERN_MAX_STATES,
            sizeof(*support->automaton));
    if (support->automaton == NULL) {
        return PASSLIT_MEMORY;
    }
    for (c = 0; c < GROUP_COUNT; ++c) {
        group_count += support->sizes[c] != 0 ? 1U : 0U;
    }
    support->automaton_count = 1;
    slots[0] = 1;
    for (i = 0; i < support->automaton_count; ++i) {
        state = &support->automaton[i];
        memset(used, 0, sizeof(used));
        distinct = 0;
        for (j = 0; j < state->length; ++j) {
            if (state->symbols[j] == distinct) {
                state->group[distinct++] = state->groups[j];
                ++used[state->groups[j]];
            }
        }
        for (choice = 0; choice < PATTERN_CHOICES; ++choice) {
            if (choice < PATTERN_SYMBOLS) {
                if (choice >= distinct) {
                    continue;
                }
                symbol = (unsigned char)(choice);
                group = state->group[choice];
                weight = 1;
            } else {
                group = (unsigned char)(choice - PATTERN_SYMBOLS);
                symbol = PATTERN_SYMBOLS;
                weight = support->sizes[group] - used[group];
            }
            if (weight == 0 ||
                    !pattern_step(&candidate, state, symbol, group,
                    group_count)) {
                continue;
            }
            key = pattern_key(&candidate);
            /* Knuth multiplicative hashing, 32-bit golden-ratio factor. */
            hash = (key ^ (key >> 16)) * UINT32_C(2654435761);
            slot = hash & (PATTERN_HASH_SLOTS - 1U);
            while (slots[slot] != 0 &&
                    pattern_key(&support->automaton[slots[slot] - 1]) != key) {
                slot = (slot + 1) & (PATTERN_HASH_SLOTS - 1U);
            }
            if (slots[slot] == 0) {
                index = support->automaton_count++;
                support->automaton[index] = candidate;
                slots[slot] = index + 1;
            }
            state->next[choice] = slots[slot] - 1;
            state->weight[choice] = (unsigned char)(weight);
            state->group[choice] = group;
        }
    }
    return PASSLIT_OK;
}

static int pattern_row(struct sampling_stats *support) {
    mpz_t *row, *previous;
    size_t entries = support->states * support->automaton_count;
    size_t i, mask, choice, next;
    const struct pattern_state *state;
    row = malloc(entries * sizeof(*row));
    if (row == NULL) {
        return PASSLIT_MEMORY;
    }
    for (i = 0; i < entries; ++i) {
        mpz_init(row[i]);
    }
    support->pattern_rows[support->rows] = row;
    if (support->rows == 0) {
        for (i = 0; i < support->automaton_count; ++i) {
            mpz_set_ui(row[i * support->states], 1);
        }
    } else {
        previous = support->pattern_rows[support->rows - 1];
        for (i = 0; i < support->automaton_count; ++i) {
            state = &support->automaton[i];
            for (mask = 0; mask < support->states; ++mask) {
                for (choice = 0; choice < PATTERN_CHOICES; ++choice) {
                    if (state->weight[choice] == 0) {
                        continue;
                    }
                    next = mask & ~(size_t)(support->needs[
                            state->group[choice]]);
                    mpz_addmul_ui(row[i * support->states + mask],
                            previous[state->next[choice] * support->states +
                            next], state->weight[choice]);
                }
            }
        }
    }
    ++support->rows;
    return PASSLIT_OK;
}

static int pattern_prepare(struct sampling_stats *support,
        const struct passlit_config *config,
        size_t maximum) {
    mpz_t target;
    mpz_srcptr count;
    size_t limit, n, i, entries;
    int status, comparison, monotone = 0;
    status = pattern_graph(support);
    if (status != PASSLIT_OK) {
        return status;
    }
    entries = support->states * support->automaton_count;
    limit = config->size_kind == PASSLIT_LENGTH ? config->size_val : maximum;
    if (config->size_kind == PASSLIT_BITS_DOWN) {
        ++limit;
    }
    support->pattern_rows = calloc(limit + 1, sizeof(*support->pattern_rows));
    if (support->pattern_rows == NULL) {
        support_clear(support);
        return PASSLIT_MEMORY;
    }
    mpz_init_set_ui(target, 1);
    if (config->size_kind != PASSLIT_LENGTH) {
        mpz_mul_2exp(target, target, (mp_bitcnt_t)(config->size_val));
    }
    status = PASSLIT_RANGE;
    for (n = 0; n <= limit; ++n) {
        if (pattern_row(support) != PASSLIT_OK) {
            status = PASSLIT_MEMORY;
            break;
        }
        if (n == 0) {
            continue;
        }
        /* A nonnegative recurrence preserves componentwise ordering. */
        if (!monotone && config->size_kind == PASSLIT_BITS_DOWN) {
            monotone = 1;
            for (i = 0; i < entries; ++i) {
                if (mpz_cmp(support->pattern_rows[n][i],
                        support->pattern_rows[n - 1][i]) < 0) {
                    monotone = 0;
                    break;
                }
            }
        }
        count = support->pattern_rows[n][support->states - 1];
        if (config->size_kind == PASSLIT_LENGTH) {
            if (n == limit && mpz_sgn(count) > 0) {
                support->length = n;
                status = PASSLIT_OK;
            }
            continue;
        }
        comparison = mpz_cmp(count, target);
        if (config->size_kind == PASSLIT_BITS_UP && comparison >= 0) {
            support->length = n;
            status = PASSLIT_OK;
            break;
        }
        if (config->size_kind == PASSLIT_BITS_DOWN) {
            if (comparison <= 0 && mpz_sgn(count) > 0) {
                support->length = n;
            }
            if (comparison > 0 && monotone) {
                status = support->length != 0 ? PASSLIT_OK : PASSLIT_RANGE;
                break;
            }
        }
    }
    mpz_clear(target);
    if (status != PASSLIT_OK) {
        support_clear(support);
    }
    return status;
}

static int pattern_walk(char *out, const char *sample,
        const struct sampling_stats *support, mpz_t rank) {
    char history[PATTERN_HISTORY + 1];
    const struct pattern_state *state;
    mpz_srcptr block;
    size_t node = 0, mask = support->states - 1;
    size_t position, c, j, k, choice, next, remaining;
    int found;
    for (position = 0; position < support->length; ++position) {
        state = &support->automaton[node];
        remaining = support->length - position - 1;
        found = 0;
        for (c = 0; c < GROUP_COUNT && !found; ++c) {
            next = mask & ~(size_t)(support->needs[c]);
            for (j = 0; j < support->sizes[c]; ++j) {
                choice = PATTERN_SYMBOLS + c;
                for (k = 0; k < state->length; ++k) {
                    if (history[k] == support->letters[c][j]) {
                        choice = state->symbols[k];
                        break;
                    }
                }
                if (state->weight[choice] == 0) {
                    continue;
                }
                block = support->pattern_rows[remaining][
                    state->next[choice] * support->states + next];
                if (sample != NULL ?
                        sample[position] == support->letters[c][j] &&
                        mpz_sgn(block) != 0 : mpz_cmp(rank, block) < 0) {
                    if (out != NULL) {
                        out[position] = support->letters[c][j];
                    }
                    history[state->length] = support->letters[c][j];
                    node = state->next[choice];
                    memmove(history,
                            history + state->length + 1 -
                            support->automaton[node].length,
                            support->automaton[node].length);
                    mask = next;
                    found = 1;
                    break;
                }
                if (sample != NULL) {
                    mpz_add(rank, rank, block);
                } else {
                    mpz_sub(rank, rank, block);
                }
            }
        }
        if (!found) {
            return PASSLIT_RANGE;
        }
    }
    if (out != NULL) {
        out[support->length] = '\0';
    }
    return mask == 0 ? PASSLIT_OK : PASSLIT_RANGE;
}

static size_t default_length(const struct sampling_stats *support,
        const struct passlit_config *config) {
    size_t c;
    if (config->size_kind == PASSLIT_LENGTH ||
            config->size_val != PASSLIT_DEFAULT_BITS ||
            support->sizes[GROUP_CUSTOM] != 0) {
        return 0;
    }
    for (c = 0; c < BUILTIN_GROUP_COUNT; ++c) {
        if (support->needs[c] == 0 || support->letters[c] == NULL ||
                strcmp(support->letters[c],
                alphabets[c][POOL_STANDARD]) != 0) {
            return 0;
        }
    }
    /* Both pattern policies bracket 2**128 at lengths 22 and 23. */
    return config->size_kind == PASSLIT_BITS_DOWN ?
        DEFAULT_LENGTH_DOWN : DEFAULT_LENGTH_UP;
}

/* counts[n, mask]: suffix count with mask still required. */
static int support_prepare(struct sampling_stats *support,
        const struct passlit_config *config) {
    struct passlit_config exact;
    mpz_t target;
    size_t maximum = passlit_length_limit();
    size_t limit, n, mask, c, entries, next, total = 0, required_count = 0;
    int status, comparison;
    memset(support, 0, sizeof(*support));
    if (config->size_kind < PASSLIT_LENGTH ||
            config->size_kind > PASSLIT_BITS_DOWN ||
            (config->size_kind == PASSLIT_LENGTH && config->size_val == 0)) {
        return PASSLIT_INVALID;
    }
    status = parse_groups(support, config->groups, config->custom);
    if (status != PASSLIT_OK) {
        return status;
    }
    if ((config->size_kind == PASSLIT_LENGTH && config->size_val > maximum) ||
            (config->size_kind != PASSLIT_LENGTH &&
            config->size_val > CHAR_BIT * maximum)) {
        return PASSLIT_RANGE;
    }
    for (c = 0; c < GROUP_COUNT; ++c) {
        total += support->sizes[c];
        required_count += support->needs[c] != 0 ? 1U : 0U;
    }
    if (config->size_kind == PASSLIT_LENGTH &&
            config->size_val < required_count) {
        return PASSLIT_RANGE;
    }
    /* Singleton: zero bits forever; no finite downward maximum. */
    if (total == 1 && config->size_kind != PASSLIT_LENGTH &&
            (config->size_kind == PASSLIT_BITS_DOWN ||
            config->size_val != 0)) {
        return PASSLIT_RANGE;
    }
    n = default_length(support, config);
    if (n != 0) {
        exact = *config;
        exact.size_kind = PASSLIT_LENGTH;
        exact.size_val = n;
        config = &exact;
    }
    if (config->patterns && total > 1) {
        return pattern_prepare(support, config, maximum);
    }
    limit = config->size_kind == PASSLIT_LENGTH ? config->size_val : maximum;
    if (config->size_kind == PASSLIT_BITS_DOWN) {
        ++limit;  /* Bracket the target. */
    }
    entries = (limit + 1) * support->states;
    support->counts = malloc(entries * sizeof(*support->counts));
    if (support->counts == NULL) {
        return PASSLIT_MEMORY;
    }
    support->rows = limit + 1;
    for (n = 0; n < entries; ++n) {
        mpz_init(support->counts[n]);
    }
    mpz_set_ui(support->counts[0], 1);
    mpz_init_set_ui(target, 1);
    if (config->size_kind != PASSLIT_LENGTH) {
        mpz_mul_2exp(target, target, (mp_bitcnt_t)(config->size_val));
    }
    status = PASSLIT_RANGE;
    for (n = 1; n <= limit; ++n) {
        for (mask = 0; mask < support->states; ++mask) {
            for (c = 0; c < GROUP_COUNT; ++c) {
                if (support->sizes[c] != 0) {
                    next = mask & ~(size_t)(support->needs[c]);
                    mpz_addmul_ui(
                            support->counts[n * support->states + mask],
                            support->counts[(n - 1) * support->states + next],
                            (unsigned long)(support->sizes[c]));
                }
            }
        }
        if (config->size_kind == PASSLIT_LENGTH) {
            if (n == limit &&
                    mpz_sgn(support->counts[
                    (n + 1) * support->states - 1]) > 0) {
                support->length = n;
                status = PASSLIT_OK;
            }
            continue;
        }
        comparison =
            mpz_cmp(support->counts[(n + 1) * support->states - 1], target);
        if (config->size_kind == PASSLIT_BITS_UP && comparison >= 0) {
            support->length = n;
            status = PASSLIT_OK;
            break;
        }
        if (config->size_kind == PASSLIT_BITS_DOWN) {
            if (comparison > 0) {
                status = support->length != 0 ? PASSLIT_OK : PASSLIT_RANGE;
                break;
            }
            if (mpz_sgn(support->counts[
                    (n + 1) * support->states - 1]) > 0) {
                support->length = n;
            }
            if (comparison == 0) {
                status =
                    support->length <= maximum ? PASSLIT_OK : PASSLIT_RANGE;
                break;
            }
            /* No clamping of unattainable targets. */
        }
    }
    mpz_clear(target);
    if (status != PASSLIT_OK) {
        support_clear(support);
    }
    return status;
}

enum { SEED_DIGEST_BYTES = 32, SEED_INDEX_BYTES = 8 };

static void encode_index(unsigned char *out, uint64_t value) {
    size_t i;
    for (i = SEED_INDEX_BYTES; i != 0; --i) {
        out[i - 1] = (unsigned char)(value & 0xffU);
        value >>= 8;
    }
}

static int seeded_bytes(unsigned char *out, size_t length,
        const unsigned char *key, uint64_t sample_index,
        uint64_t *counter) {
    unsigned char block[SEED_DIGEST_BYTES];
    unsigned char input[sizeof("passlit-v1") - 1 + 2 * SEED_INDEX_BYTES];
    size_t portion;
    memcpy(input, "passlit-v1", sizeof("passlit-v1") - 1);
    encode_index(input + sizeof("passlit-v1") - 1, sample_index);
    while (length != 0) {
        encode_index(input + sizeof("passlit-v1") - 1 + SEED_INDEX_BYTES,
                *counter);
        ++*counter;
        if (HMAC(EVP_sha256(), key, SEED_DIGEST_BYTES, input, sizeof(input),
                block, NULL) == NULL) {
            OPENSSL_cleanse(block, sizeof(block));
            return 0;
        }
        portion = length < sizeof(block) ? length : sizeof(block);
        memcpy(out, block, portion);
        out += portion;
        length -= portion;
    }
    OPENSSL_cleanse(block, sizeof(block));
    return 1;
}

/* Uniform ordinal in [0, count); rejection stays here. */
static int random_rank(mpz_t rank, const mpz_t count,
        const char *seed, uint64_t sample_index) {
    unsigned char key[SEED_DIGEST_BYTES];
    uint64_t counter = 0;
    unsigned char *bytes;
    size_t bits, length;
    int status = PASSLIT_OK;

    mpz_sub_ui(rank, count, 1);
    if (mpz_sgn(rank) == 0) {
        return PASSLIT_OK;
    }
    bits = mpz_sizeinbase(rank, 2);
    length = bits / 8 + ((bits & 7U) != 0 ? 1U : 0U);
    bytes = malloc(length);
    if (bytes == NULL) {
        return PASSLIT_MEMORY;
    }
    if (seed != NULL &&
            EVP_Digest(seed, strlen(seed), key, NULL,
            EVP_sha256(), NULL) != 1) {
        OPENSSL_cleanse(key, sizeof(key));
        free(bytes);
        return PASSLIT_RANDOM;
    }
    do {
        if ((seed != NULL
                ? seeded_bytes(bytes, length, key, sample_index, &counter)
                : RAND_bytes(bytes, (int)(length))) != 1) {
            status = PASSLIT_RANDOM;
            break;
        }
        if ((bits & 7U) != 0) {
            bytes[0] &= (unsigned char)((1U << ((bits & 7U))) - 1U);
        }
        mpz_import(rank, length, 1, 1, 0, 0, bytes);
    } while (mpz_cmp(rank, count) >= 0);
    OPENSSL_cleanse(key, sizeof(key));
    free(bytes);
    return status;
}

/* Each next character owns a block of suffix ordinals. */
static int walk_sample(char *out, const char *sample,
        const struct sampling_stats *support, mpz_t rank) {
    mpz_srcptr block;
    size_t position, c, j, remaining;
    unsigned int next, mask = (unsigned int)(support->states - 1);
    int found;
    if (support->pattern_rows != NULL) {
        return pattern_walk(out, sample, support, rank);
    }
    for (position = 0; position < support->length; ++position) {
        found = 0;
        remaining = support->length - position - 1;
        for (c = 0; c < GROUP_COUNT && !found; ++c) {
            next = mask & ~support->needs[c];
            for (j = 0; j < support->sizes[c]; ++j) {
                block = support->counts[remaining * support->states + next];
                if (sample != NULL ?
                        sample[position] == support->letters[c][j] &&
                        mpz_sgn(block) != 0 : mpz_cmp(rank, block) < 0) {
                    if (out != NULL) {
                        out[position] = support->letters[c][j];
                    }
                    mask = next;
                    found = 1;
                    break;
                }
                if (sample != NULL) {
                    mpz_add(rank, rank, block);
                } else {
                    mpz_sub(rank, rank, block);
                }
            }
        }
        if (!found) {
            return PASSLIT_RANGE;
        }
    }
    if (out != NULL) {
        out[support->length] = '\0';
    }
    return mask == 0 ? PASSLIT_OK : PASSLIT_RANGE;
}

static void unrank_sample(char *out, const struct sampling_stats *support,
        mpz_t rank) {
    (void)walk_sample(out, NULL, support, rank);
}

mpz_srcptr sampling_count(const struct sampling_stats *support) {
    if (support->pattern_rows != NULL) {
        return support->pattern_rows[support->length][support->states - 1];
    }
    return support->counts[(support->length + 1) * support->states - 1];
}

int sampling_encode(char *out, size_t out_size,
        const struct sampling_stats *support, mpz_srcptr rank) {
    mpz_t copy;
    if (out == NULL || out_size == 0) {
        return PASSLIT_INVALID;
    }
    *out = '\0';
    if (out_size <= support->length || mpz_sgn(rank) < 0 ||
            mpz_cmp(rank, sampling_count(support)) >= 0) {
        return PASSLIT_RANGE;
    }
    mpz_init_set(copy, rank);
    unrank_sample(out, support, copy);
    mpz_clear(copy);
    return PASSLIT_OK;
}

int sampling_rank(mpz_t out, const struct sampling_stats *support,
        const char *sample) {
    int status = PASSLIT_RANGE;
    mpz_set_ui(out, 0);
    if (sample != NULL && strlen(sample) == support->length) {
        status = walk_sample(NULL, sample, support, out);
    }
    if (status != PASSLIT_OK) {
        mpz_set_si(out, -1);
    }
    return status;
}

int sampling_prepare(struct sampling_stats **support,
        const struct passlit_config *config) {
    struct sampling_stats *prepared;
    int status;
    *support = NULL;
    prepared = malloc(sizeof(*prepared));
    if (prepared == NULL) {
        return PASSLIT_MEMORY;
    }
    status = support_prepare(prepared, config);
    if (status != PASSLIT_OK) {
        free(prepared);
        return status;
    }
    *support = prepared;
    return PASSLIT_OK;
}

size_t sampling_length(const struct sampling_stats *support) {
    return support->length;
}

void sampling_free(struct sampling_stats *support) {
    support_clear(support);
    free(support);
}

int sampling_sample(char *out, size_t out_size,
        const struct sampling_stats *support,
        const char *seed, uint64_t index) {
    mpz_t rank;
    int status;
    if (out == NULL || out_size == 0) {
        return PASSLIT_INVALID;
    }
    out[0] = '\0';
    if (support->length >= out_size) {
        return PASSLIT_RANGE;
    }
    mpz_init(rank);
    status = random_rank(
            rank, sampling_count(support),
            seed, index);
    if (status == PASSLIT_OK) {
        status = sampling_encode(out, out_size, support, rank);
    }
    mpz_clear(rank);
    return status;
}

