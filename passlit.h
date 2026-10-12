/* Licensed under the GNU Lesser General Public License, version 3 only. */

#ifndef PASSLIT_H
#define PASSLIT_H

#include <stddef.h>
#include <stdint.h>

#include <gmp.h>

#define PASSLIT_VERSION 0x00000100U  /* major.minor.patch.reserved */

#define PASSLIT_MIN_LENGTH_LIMIT 4096U
#define PASSLIT_CUSTOM_MAX_LENGTH 256U
#define PASSLIT_DEFAULT_BITS 128U
#define PASSLIT_SAMPLES_PER_JOB 16U
#define PASSLIT_PARALLEL_MIN_SAMPLES 256U
#define PASSLIT_DEFAULT_CONFIG { \
    .groups = NULL, .custom = NULL, .seed = NULL, \
    .sample_index = 0, .count = 1, .workers = 0, \
    .size_val = PASSLIT_DEFAULT_BITS, \
    .size_kind = PASSLIT_BITS_DOWN, \
    .patterns = 1 \
}

/* Size units and rounding. */
enum passlit_size_kind {
    PASSLIT_LENGTH,
    PASSLIT_BITS_UP,
    PASSLIT_BITS_DOWN
};

/* Status; sysexits-style values. */
enum passlit_status {
    PASSLIT_OK = 0,
    PASSLIT_INVALID = 64,
    PASSLIT_RANGE = 65,
    PASSLIT_RANDOM = 74,
    PASSLIT_MEMORY = 71
};

struct passlit_config {
    /* First seeded sample index. */
    uint64_t sample_index;
    /* NULL: U*L*D*O*. */
    const char *groups;
    /* Custom non-whitespace bytes. */
    const char *custom;
    /* NULL: random; otherwise deterministic. */
    const char *seed;
    /* Sample count. */
    size_t count;
    /* 0: automatic. */
    size_t workers;
    /* Length or bit target. */
    size_t size_val;
    /* Units and rounding. */
    enum passlit_size_kind size_kind;
    /* Nonzero: reject patterns. */
    int patterns;
};

/* Pack fields; borrow strings. */
struct passlit_config passlit_configure(const char *groups, const char *custom,
        enum passlit_size_kind size_kind, size_t size_val,
        const char *seed, uint64_t sample_index,
        int patterns, size_t count, size_t workers);

/* Packed samples: sample\0sample\0\0. See passlit(3). */
int passlit_generate(char *out, size_t out_size,
        const struct passlit_config *config);
int passlit_generate2(char *out, size_t out_size, const char *groups,
        const char *custom,
        enum passlit_size_kind size_kind, size_t size_val,
        const char *seed, uint64_t sample_index,
        int patterns, size_t count, size_t workers);

/* Caller frees *outp. */
int passlit_generatep(char **outp, const struct passlit_config *config);
int passlit_generatep2(char **outp, const char *groups, const char *custom,
        enum passlit_size_kind size_kind, size_t size_val,
        const char *seed, uint64_t sample_index,
        int patterns, size_t count, size_t workers);

/* Exact count and length; initialize total first. */
int passlit_scope(mpz_t total, size_t *length,
        const struct passlit_config *config);

/* Zero-based ranks to packed samples; ranks are read-only. */
int passlit_encode(char *out, size_t out_size, mpz_t *ranks,
        const struct passlit_config *config);
int passlit_encodep(char **outp, mpz_t *ranks,
        const struct passlit_config *config);

/* Initialize out integers first. Invalid samples get -1; status RANGE. */
int passlit_rank(mpz_t *out, const char *const *samples,
        const struct passlit_config *config);

/* Allocates integers, including -1 entries on RANGE. Clear each, then free. */
int passlit_rankp(mpz_t **outp, const char *const *samples,
        const struct passlit_config *config);

/* Explicit-argument wrappers. */
int passlit_scope2(mpz_t total, size_t *length,
        const char *groups, const char *custom,
        enum passlit_size_kind size_kind, size_t size_val,
        int patterns);
int passlit_encode2(char *out, size_t out_size, mpz_t *ranks,
        const char *groups, const char *custom,
        enum passlit_size_kind size_kind, size_t size_val,
        int patterns, size_t count, size_t workers);
int passlit_encodep2(char **outp, mpz_t *ranks,
        const char *groups, const char *custom,
        enum passlit_size_kind size_kind, size_t size_val,
        int patterns, size_t count, size_t workers);
int passlit_rank2(mpz_t *out, const char *const *samples,
        const char *groups, const char *custom,
        enum passlit_size_kind size_kind, size_t size_val,
        int patterns, size_t count, size_t workers);
int passlit_rankp2(mpz_t **outp, const char *const *samples,
        const char *groups, const char *custom,
        enum passlit_size_kind size_kind, size_t size_val,
        int patterns, size_t count, size_t workers);

#endif
