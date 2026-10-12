/* Scripted RNG. */
#include "../passlit.h"

#include <stdio.h>
#include <assert.h>
#include <string.h>

#include <openssl/rand.h>

static unsigned int calls;
static int fail_after_block;
static unsigned char first_byte, second_byte;

static int scripted_bytes(unsigned char *buffer, int length) {
    ++calls;
    if (fail_after_block && calls > 1) {
        return 0;
    }
    memset(buffer, fail_after_block ? 255 : 0, (size_t)(length));
    buffer[0] = calls == 1 ? first_byte : second_byte;
    return 1;
}

#define RAND_bytes scripted_bytes
#include "../sampling.c"
#include "../passlit.c"
#undef RAND_bytes

int main(void) {
    struct passlit_config config = PASSLIT_DEFAULT_CONFIG;
    mpz_t rank, count;
    char *out = NULL;
    mpz_inits(rank, count, NULL);
    mpz_set_ui(count, 1);
    assert(random_rank(rank, count, NULL, 0) == PASSLIT_OK);
    assert(mpz_cmp_ui(rank, 0) == 0 && calls == 0);

    /* Count 5: reject 7, accept 4. */
    mpz_set_ui(count, 5);
    first_byte = 7;
    second_byte = 4;
    assert(random_rank(rank, count, NULL, 0) == PASSLIT_OK);
    assert(mpz_cmp_ui(rank, 4) == 0 && calls == 2);

    /* 0xfa masks to 2. */
    calls = 0;
    first_byte = 0xfa;
    assert(random_rank(rank, count, NULL, 0) == PASSLIT_OK);
    assert(mpz_cmp_ui(rank, 2) == 0 && calls == 1);

    calls = 0;
    mpz_set_ui(count, 256);
    first_byte = 0xab;
    assert(random_rank(rank, count, NULL, 0) == PASSLIT_OK);
    assert(mpz_cmp_ui(rank, 171) == 0 && calls == 1);

    /* Retry failure propagates. */
    calls = 0;
    mpz_set_ui(count, 3);
    first_byte = 255;
    fail_after_block = 1;
    assert(random_rank(rank, count, NULL, 0) == PASSLIT_RANDOM);
    assert(calls == 2);

    assert(passlit_generatep(&out, &config) == PASSLIT_RANDOM);
    assert(out == NULL);

    mpz_clears(rank, count, NULL);
    puts("Uniform random-integer checks passed.");
    return 0;
}
