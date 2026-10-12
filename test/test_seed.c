/* Independent SHA-256/HMAC vectors. */
#include "../passlit.h"

#include <stdio.h>
#include <assert.h>

#include <openssl/rand.h>

static int unexpected_random(unsigned char *out, int size) {
    (void)(out);
    (void)(size);
    assert(!"seeded generation must not request system randomness");
    return 0;
}

#define RAND_bytes unexpected_random
#include "../sampling.c"
#include "../passlit.c"
#undef RAND_bytes

enum { LONG_SAMPLE_LENGTH = 600 };

int main(void) {
    char output[LONG_SAMPLE_LENGTH + 2], repeat[LONG_SAMPLE_LENGTH + 2];
    static const char expected[] =
        "e38e974934250fdb453dfd5d486a5e35c5c2bdb39b1114a80daab067d57bb965"
        "04fbbe3a29c6c89fe4847ce1e21d27a2ae1f944711f9d33c0446c7ea59d22cee"
        "0de1fc92703f14f13a02c48d65152cd091d3fe88d7bf0ba6fba4ccf0e4fc9d64";
    unsigned char bytes[96];
    struct passlit_config config = {
        .count = 1, .workers = 1, .groups = NULL, .size_val = 128U,
        .size_kind = PASSLIT_BITS_DOWN, .patterns = 0 };
    struct sampling_stats *support;
    unsigned char key[SEED_DIGEST_BYTES];
    mpz_t ordinal, count;
    uint64_t counter = 0;
    size_t i;
    int policy;

    assert(EVP_Digest("example", 7, key, NULL, EVP_sha256(), NULL) == 1);
    assert(seeded_bytes(bytes, sizeof(bytes), key, 0, &counter) == 1);
    assert(counter == 3);
    for (i = 0; i < sizeof(bytes); ++i) {
        assert("0123456789abcdef"[bytes[i] >> 4] == expected[2 * i]);
        assert("0123456789abcdef"[bytes[i] & 15U] == expected[2 * i + 1]);
    }
    mpz_inits(ordinal, count, NULL);
    mpz_set_ui(count, 3);
    assert(random_rank(ordinal, count, "example", 0) == PASSLIT_OK);
    assert(mpz_cmp_ui(ordinal, 0) == 0);  /* Rejected 3. */
    mpz_clears(ordinal, count, NULL);

    config.groups = "c*";
    config.custom = "abcd";
    config.seed = "example";
    config.size_kind = PASSLIT_LENGTH;
    config.size_val = 8;
    assert(passlit_generate(output, sizeof(output), &config) ==
            PASSLIT_OK);
    assert(strcmp(output, "dcadcadc") == 0);
    assert(passlit_generate2(repeat, sizeof(repeat), "c*", "abcd",
            PASSLIT_LENGTH, 8, "example", 0,
            0, 1, 1) == PASSLIT_OK);
    assert(strcmp(output, repeat) == 0);
    config.size_val = 8;
    for (policy = 0; policy <= 1;
            ++policy) {
        config.patterns = policy;
        assert(sampling_prepare(&support, &config) == PASSLIT_OK);
        assert(sampling_sample(repeat, 1, support, config.seed, 0) ==
                PASSLIT_RANGE);
        for (i = 0; i < 20; ++i) {
            config.sample_index = (uint64_t)(i);
            assert(passlit_generate(output, sizeof(output), &config)
                    == PASSLIT_OK);
            assert(sampling_sample(repeat, sizeof(repeat), support,
                    config.seed, config.sample_index) ==
                    PASSLIT_OK);
            assert(strcmp(output, repeat) == 0);
        }
        sampling_free(support);
    }
    config.patterns = 0;
    config.sample_index = 1;
    assert(passlit_generate(output, sizeof(output), &config) ==
            PASSLIT_OK);
    assert(strcmp(output, "addaadcb") == 0);
    config.sample_index = UINT64_MAX;
    assert(passlit_generate(output, sizeof(output), &config) ==
            PASSLIT_OK);
    assert(strcmp(output, "bacddcab") == 0);
    config.sample_index = 0;
    config.seed = "";
    assert(passlit_generate(output, sizeof(output), &config) ==
            PASSLIT_OK);
    assert(strcmp(output, "adcaddab") == 0);
    config.size_val = LONG_SAMPLE_LENGTH;
    assert(passlit_generate(output, sizeof(output), &config) ==
            PASSLIT_OK);
    assert(passlit_generate(repeat, sizeof(repeat), &config) ==
            PASSLIT_OK);
    assert(strlen(output) == LONG_SAMPLE_LENGTH);
    assert(strspn(output, "abcd") == LONG_SAMPLE_LENGTH);
    assert(strcmp(output, repeat) == 0);
    puts("Deterministic stream, rejection, indexing, and sample checks "
            "passed.");
    return 0;
}
