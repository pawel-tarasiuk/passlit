#ifndef PASSLIT_SAMPLING_H
#define PASSLIT_SAMPLING_H

#include "passlit.h"

struct sampling_stats;

/* Private batch interface; prepared counts are immutable. */
int sampling_prepare(struct sampling_stats **support,
        const struct passlit_config *config);
size_t sampling_length(const struct sampling_stats *support);
void sampling_free(struct sampling_stats *support);
int sampling_sample(char *out, size_t out_size,
        const struct sampling_stats *support,
        const char *seed, uint64_t index);

mpz_srcptr sampling_count(const struct sampling_stats *support);
int sampling_encode(char *out, size_t out_size,
        const struct sampling_stats *support, mpz_srcptr rank);
int sampling_rank(mpz_t out, const struct sampling_stats *support,
        const char *sample);

/* Immutable prepared sampler; copies the seed, retains no config pointers. */
struct passlit_sampler;
int passlit_sampler_create(struct passlit_sampler **outp,
        const struct passlit_config *config);
void passlit_sampler_free(struct passlit_sampler *sampler);
size_t passlit_sampler_length(const struct passlit_sampler *sampler);

mpz_srcptr passlit_sampler_count(const struct passlit_sampler *sampler);
int passlit_sampler_encode(char *out, size_t out_size,
        const struct passlit_sampler *sampler, mpz_srcptr rank);
int passlit_sampler_rank(mpz_t out, const struct passlit_sampler *sampler,
        const char *sample);

/* Conversion batches preserve slots on range errors: empty string or -1.
 * Input/output arrays have count entries; encoded slots use length + 1.
 * Worker failures are fatal; no output should be consumed after them. */
int passlit_sampler_encode_batch(const struct passlit_sampler *sampler,
        char *out, mpz_t *ranks, size_t count, size_t workers);
int passlit_sampler_rank_batch(const struct passlit_sampler *sampler,
        mpz_t *out, const char *const *samples, size_t count, size_t workers);

/* offset is added to config.sample_index; overflow returns PASSLIT_RANGE. */
int passlit_sampler_sample(char *out, size_t out_size,
        const struct passlit_sampler *sampler, uint64_t offset);

/* Sample is borrowed until the callback returns. Return zero to continue;
 * other values stop the batch and are returned unchanged. Earlier samples
 * remain delivered on failure. Callbacks run in order on the calling thread.
 * count must be positive; workers > 1 needs THREADS=1.
 * workers == 0 selects half the online CPUs, or one without threads.
 * Batches below 256 samples run serially. */
typedef int (*passlit_emit)(const char *sample, void *context);
int passlit_sampler_batch(const struct passlit_sampler *sampler,
        uint64_t offset, size_t count, size_t workers,
        passlit_emit emit, void *context);

#endif
