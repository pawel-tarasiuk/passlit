/* Licensed under the GNU Lesser General Public License, version 3 only. */

#include "passlit.h"
#include "platform.h"
#include "sampling.h"

#include <stdlib.h>
#include <string.h>

#ifdef PASSLIT_THREADS
    #include <pthread.h>
#endif

struct passlit_config passlit_configure(const char *groups, const char *custom,
        enum passlit_size_kind size_kind, size_t size_val,
        const char *seed, uint64_t sample_index,
        int patterns, size_t count, size_t workers) {
    struct passlit_config config = {
        .sample_index = sample_index, .groups = groups, .custom = custom,
        .seed = seed, .count = count, .workers = workers,
        .size_val = size_val, .size_kind = size_kind, .patterns = patterns
    };
    return config;
}

struct passlit_sampler {
    uint64_t index;
    struct sampling_stats *support;
    char *seed;
};

void passlit_sampler_free(struct passlit_sampler *sampler) {
    if (sampler != NULL) {
        sampling_free(sampler->support);
        free(sampler->seed);
        free(sampler);
    }
}

int passlit_sampler_create(struct passlit_sampler **outp,
        const struct passlit_config *config) {
    struct passlit_sampler *sampler;
    size_t length;
    int status;

    if (outp == NULL) {
        return PASSLIT_INVALID;
    }
    *outp = NULL;
    if (config == NULL) {
        return PASSLIT_INVALID;
    }
    sampler = calloc(1, sizeof(*sampler));
    if (sampler == NULL) {
        return PASSLIT_MEMORY;
    }
    status = sampling_prepare(&sampler->support, config);
    if (status != PASSLIT_OK) {
        free(sampler);
        return status;
    }
    sampler->index = config->sample_index;
    if (config->seed != NULL) {
        length = strlen(config->seed) + 1;
        sampler->seed = malloc(length);
        if (sampler->seed == NULL) {
            passlit_sampler_free(sampler);
            return PASSLIT_MEMORY;
        }
        memcpy(sampler->seed, config->seed, length);
    }
    *outp = sampler;
    return PASSLIT_OK;
}

size_t passlit_sampler_length(const struct passlit_sampler *sampler) {
    return sampler == NULL ? 0 : sampling_length(sampler->support);
}

int passlit_sampler_sample(char *out, size_t out_size,
        const struct passlit_sampler *sampler, uint64_t offset) {
    if (out != NULL && out_size != 0) {
        *out = '\0';
    }
    if (sampler == NULL) {
        return PASSLIT_INVALID;
    }
    if (offset > UINT64_MAX - sampler->index) {
        return PASSLIT_RANGE;
    }
    return sampling_sample(out, out_size, sampler->support, sampler->seed,
            sampler->index + offset);
}

mpz_srcptr passlit_sampler_count(const struct passlit_sampler *sampler) {
    return sampling_count(sampler->support);
}

int passlit_sampler_encode(char *out, size_t out_size,
        const struct passlit_sampler *sampler, mpz_srcptr rank) {
    return sampling_encode(out, out_size, sampler->support, rank);
}

int passlit_sampler_rank(mpz_t out, const struct passlit_sampler *sampler,
        const char *sample) {
    return sampling_rank(out, sampler->support, sample);
}

int passlit_scope(mpz_t total, size_t *length,
        const struct passlit_config *config) {
    struct sampling_stats *support;
    int status;
    mpz_set_ui(total, 0);
    if (length == NULL) {
        return PASSLIT_INVALID;
    }
    *length = 0;
    if (config == NULL) {
        return PASSLIT_INVALID;
    }
    status = sampling_prepare(&support, config);
    if (status == PASSLIT_OK) {
        *length = sampling_length(support);
        mpz_set(total, sampling_count(support));
        sampling_free(support);
    }
    return status;
}

static int rank(mpz_t **outp, const char *const *samples,
        const struct passlit_config *config, int allocated) {
    struct passlit_sampler *sampler;
    size_t i;
    int status;
    if (outp == NULL) {
        return PASSLIT_INVALID;
    }
    if (allocated) {
        *outp = NULL;
    }
    if (config == NULL ||
            (!allocated && config->count != 0 && *outp == NULL)) {
        return PASSLIT_INVALID;
    }
    if (!allocated) {
        for (i = 0; i < config->count; ++i) {
            mpz_set_si((*outp)[i], -1);
        }
    }
    if (config->count != 0 && samples == NULL) {
        return PASSLIT_INVALID;
    }
    status = passlit_sampler_create(&sampler, config);
    if (status != PASSLIT_OK) {
        return status;
    }
    if (allocated && config->count != 0) {
        if (config->count > SIZE_MAX / sizeof(**outp)) {
            passlit_sampler_free(sampler);
            return PASSLIT_RANGE;
        }
        *outp = malloc(config->count * sizeof(**outp));
        if (*outp == NULL) {
            passlit_sampler_free(sampler);
            return PASSLIT_MEMORY;
        }
        for (i = 0; i < config->count; ++i) {
            mpz_init_set_si((*outp)[i], -1);
        }
    }
    status = passlit_sampler_rank_batch(sampler, *outp, samples,
            config->count, config->workers);
    passlit_sampler_free(sampler);
    if (allocated && status != PASSLIT_OK && status != PASSLIT_RANGE) {
        for (i = 0; i < config->count; ++i) {
            mpz_clear((*outp)[i]);
        }
        free(*outp);
        *outp = NULL;
    }
    return status;
}

int passlit_rank(mpz_t *out, const char *const *samples,
        const struct passlit_config *config) {
    return rank(&out, samples, config, 0);
}

int passlit_rankp(mpz_t **outp, const char *const *samples,
        const struct passlit_config *config) {
    return rank(outp, samples, config, 1);
}

static int pack_sample(const char *sample, void *context) {
    char **next = context;
    size_t stride = strlen(sample) + 1;
    memcpy(*next, sample, stride);
    *next += stride;
    return PASSLIT_OK;
}

static int generate(char **outp, size_t out_size,
        const struct passlit_config *config, int allocated,
        mpz_t *ranks, int encoding) {
    struct passlit_sampler *sampler;
    char *next;
    size_t stride, required;
    int status;

    if (outp == NULL || (!allocated && (*outp == NULL || out_size == 0))) {
        return PASSLIT_INVALID;
    }
    if (allocated) {
        *outp = NULL;
    } else {
        (*outp)[0] = '\0';
    }
    if (config == NULL ||
            (encoding && config->count != 0 && ranks == NULL)) {
        return PASSLIT_INVALID;
    }
#ifndef PASSLIT_THREADS
    if (config->workers > 1) {
        return PASSLIT_INVALID;
    }
#endif
    if (!encoding && config->count != 0 &&
            config->count - 1 > UINT64_MAX - config->sample_index) {
        return PASSLIT_RANGE;
    }
    status = passlit_sampler_create(&sampler, config);
    if (status != PASSLIT_OK) {
        return status;
    }
    stride = passlit_sampler_length(sampler) + 1;
    if (config->count > (SIZE_MAX - 1) / stride) {
        status = PASSLIT_RANGE;
        goto done;
    }
    required = config->count * stride + 1;
    if (allocated) {
        *outp = malloc(required);
        if (*outp == NULL) {
            status = PASSLIT_MEMORY;
            goto done;
        }
    } else if (out_size < required) {
        status = PASSLIT_RANGE;
        goto done;
    }
    next = *outp;
    if (encoding) {
        status = passlit_sampler_encode_batch(sampler, *outp, ranks,
                config->count, config->workers);
        if (status != PASSLIT_OK) {
            goto done;
        }
    } else if (config->count != 0) {
        status = passlit_sampler_batch(sampler, 0, config->count,
                config->workers, pack_sample, &next);
        if (status != PASSLIT_OK) {
            goto done;
        }
    }
    (*outp)[required - 1] = '\0';
done:
    passlit_sampler_free(sampler);
    if (status != PASSLIT_OK) {
        if (allocated) {
            free(*outp);
            *outp = NULL;
        } else {
            **outp = '\0';
        }
    }
    return status;
}

int passlit_encode(char *out, size_t out_size, mpz_t *ranks,
        const struct passlit_config *config) {
    return generate(&out, out_size, config, 0, ranks, 1);
}

int passlit_encodep(char **outp, mpz_t *ranks,
        const struct passlit_config *config) {
    return generate(outp, 0, config, 1, ranks, 1);
}

int passlit_generate(char *out, size_t out_size,
        const struct passlit_config *config) {
    return generate(&out, out_size, config, 0, NULL, 0);
}

int passlit_generate2(char *out, size_t out_size, const char *groups,
        const char *custom,
        enum passlit_size_kind size_kind, size_t size_val,
        const char *seed, uint64_t sample_index,
        int patterns, size_t count, size_t workers) {
    struct passlit_config config = passlit_configure(groups, custom,
            size_kind, size_val, seed, sample_index, patterns, count, workers);
    return passlit_generate(out, out_size, &config);
}

int passlit_generatep(char **outp, const struct passlit_config *config) {
    return generate(outp, 0, config, 1, NULL, 0);
}

int passlit_generatep2(char **outp, const char *groups, const char *custom,
        enum passlit_size_kind size_kind, size_t size_val,
        const char *seed, uint64_t sample_index,
        int patterns, size_t count, size_t workers) {
    struct passlit_config config = passlit_configure(groups, custom,
            size_kind, size_val, seed, sample_index, patterns, count, workers);
    return passlit_generatep(outp, &config);
}

int passlit_scope2(mpz_t total, size_t *length,
        const char *groups, const char *custom,
        enum passlit_size_kind size_kind, size_t size_val,
        int patterns) {
    struct passlit_config config = passlit_configure(groups, custom,
            size_kind, size_val, NULL, 0, patterns, 1, 0);
    return passlit_scope(total, length, &config);
}

int passlit_encode2(char *out, size_t out_size, mpz_t *ranks,
        const char *groups, const char *custom,
        enum passlit_size_kind size_kind, size_t size_val,
        int patterns, size_t count, size_t workers) {
    struct passlit_config config = passlit_configure(groups, custom,
            size_kind, size_val, NULL, 0, patterns, count, workers);
    return passlit_encode(out, out_size, ranks, &config);
}

int passlit_encodep2(char **outp, mpz_t *ranks,
        const char *groups, const char *custom,
        enum passlit_size_kind size_kind, size_t size_val,
        int patterns, size_t count, size_t workers) {
    struct passlit_config config = passlit_configure(groups, custom,
            size_kind, size_val, NULL, 0, patterns, count, workers);
    return passlit_encodep(outp, ranks, &config);
}

int passlit_rank2(mpz_t *out, const char *const *samples,
        const char *groups, const char *custom,
        enum passlit_size_kind size_kind, size_t size_val,
        int patterns, size_t count, size_t workers) {
    struct passlit_config config = passlit_configure(groups, custom,
            size_kind, size_val, NULL, 0, patterns, count, workers);
    return passlit_rank(out, samples, &config);
}

int passlit_rankp2(mpz_t **outp, const char *const *samples,
        const char *groups, const char *custom,
        enum passlit_size_kind size_kind, size_t size_val,
        int patterns, size_t count, size_t workers) {
    struct passlit_config config = passlit_configure(groups, custom,
            size_kind, size_val, NULL, 0, patterns, count, workers);
    return passlit_rankp(outp, samples, &config);
}

enum job_kind { JOB_GENERATE, JOB_ENCODE, JOB_RANK };

struct sampling_job {
    uint64_t offset;
    const struct passlit_sampler *sampler;
    char *output;
    mpz_t *ranks;
    const char *const *samples;
    size_t stride, count, base;
    enum job_kind kind;
    int status;
};

static void *sample_job(void *argument) {
    struct sampling_job *job = argument;
    size_t i, index;
    int status;
    job->status = PASSLIT_OK;
    for (i = 0; i < job->count; ++i) {
        index = job->base + i;
        if (job->kind == JOB_RANK) {
            status = passlit_sampler_rank(job->ranks[index], job->sampler,
                    job->samples[index]);
        } else if (job->kind == JOB_ENCODE) {
            status = passlit_sampler_encode(
                    job->output + index * job->stride, job->stride,
                    job->sampler, job->ranks[index]);
        } else {
            status = passlit_sampler_sample(job->output + i * job->stride,
                    job->stride, job->sampler,
                    job->offset + (uint64_t)(i));
        }
        if (status != PASSLIT_OK) {
            job->status = status;
            if (job->kind == JOB_GENERATE) {
                break;
            }
        }
    }
    return NULL;
}

#ifdef PASSLIT_THREADS
static int run_jobs(struct sampling_job *jobs, pthread_t *threads,
        size_t count) {
    size_t i, started = 0;
    for (; started < count; ++started) {
        if (pthread_create(&threads[started], NULL, sample_job,
                &jobs[started]) != 0) {
            break;
        }
    }
    for (i = 0; i < started; ++i) {
        pthread_join(threads[i], NULL);
    }
    return started == count ? PASSLIT_OK : PASSLIT_MEMORY;
}

static int parallel_samples(const struct passlit_sampler *sampler,
        uint64_t offset, size_t stride, size_t count, size_t workers,
        passlit_emit emit, void *context) {
    struct sampling_job *jobs;
    pthread_t *threads;
    size_t base = 0, i, j, started, portion, assigned;
    int status = PASSLIT_OK;
    portion = count / PASSLIT_SAMPLES_PER_JOB +
        (count % PASSLIT_SAMPLES_PER_JOB != 0);
    workers = workers < portion ? workers : portion;
    jobs = calloc(workers, sizeof(*jobs));
    threads = calloc(workers, sizeof(*threads));
    if (jobs == NULL || threads == NULL) {
        free(jobs);
        free(threads);
        return PASSLIT_MEMORY;
    }
    for (i = 0; i < workers; ++i) {
        jobs[i].output = malloc(stride * PASSLIT_SAMPLES_PER_JOB);
        if (jobs[i].output == NULL) {
            status = PASSLIT_MEMORY;
            goto done;
        }
        jobs[i].stride = stride;
        jobs[i].sampler = sampler;
    }
    while (base < count) {
        started = 0;
        assigned = 0;
        for (i = 0; i < workers && assigned < count - base; ++i) {
            portion = count - base - assigned;
            jobs[i].count = portion < PASSLIT_SAMPLES_PER_JOB ? portion :
                PASSLIT_SAMPLES_PER_JOB;
            jobs[i].offset = offset + (uint64_t)(base + assigned);
            ++started;
            assigned += jobs[i].count;
        }
        status = run_jobs(jobs, threads, started);
        if (status != PASSLIT_OK) {
            break;
        }
        for (i = 0; i < started; ++i) {
            if (jobs[i].status != PASSLIT_OK) {
                status = jobs[i].status;
                goto done;
            }
            for (j = 0; j < jobs[i].count; ++j) {
                status = emit(jobs[i].output + j * stride, context);
                if (status != PASSLIT_OK) {
                    goto done;
                }
            }
        }
        base += assigned;
    }
done:
    for (i = 0; i < workers; ++i) {
        free(jobs[i].output);
    }
    free(threads);
    free(jobs);
    return status;
}
#endif

static int convert_batch(const struct passlit_sampler *sampler,
        char *out, mpz_t *ranks, const char *const *samples,
        size_t count, size_t workers, enum job_kind kind) {
    struct sampling_job job = {0};
#ifdef PASSLIT_THREADS
    struct sampling_job *jobs;
    pthread_t *threads;
    size_t i, base, portion;
    int status;
#endif
    if (workers == 0) {
        workers = passlit_worker_count();
    }
#ifndef PASSLIT_THREADS
    if (workers != 1) {
        return PASSLIT_INVALID;
    }
#endif
    job.sampler = sampler;
    job.output = out;
    job.ranks = ranks;
    job.samples = samples;
    job.count = count;
    job.stride = passlit_sampler_length(sampler) + 1;
    job.kind = kind;
#ifdef PASSLIT_THREADS
    if (workers > 1 && count >= PASSLIT_PARALLEL_MIN_SAMPLES) {
        portion = count / PASSLIT_SAMPLES_PER_JOB +
            (count % PASSLIT_SAMPLES_PER_JOB != 0);
        workers = workers < portion ? workers : portion;
        jobs = calloc(workers, sizeof(*jobs));
        threads = calloc(workers, sizeof(*threads));
        if (jobs == NULL || threads == NULL) {
            free(jobs);
            free(threads);
            return PASSLIT_MEMORY;
        }
        base = 0;
        for (i = 0; i < workers; ++i) {
            jobs[i] = job;
            jobs[i].base = base;
            jobs[i].count = count / workers + (i < count % workers);
            base += jobs[i].count;
        }
        status = run_jobs(jobs, threads, workers);
        for (i = 0; i < workers && status != PASSLIT_MEMORY; ++i) {
            if (jobs[i].status != PASSLIT_OK) {
                status = jobs[i].status;
            }
        }
        free(jobs);
        free(threads);
        return status;
    }
#endif
    sample_job(&job);
    return job.status;
}

int passlit_sampler_encode_batch(const struct passlit_sampler *sampler,
        char *out, mpz_t *ranks, size_t count, size_t workers) {
    return convert_batch(sampler, out, ranks, NULL, count, workers,
            JOB_ENCODE);
}

int passlit_sampler_rank_batch(const struct passlit_sampler *sampler,
        mpz_t *out, const char *const *samples,
        size_t count, size_t workers) {
    return convert_batch(sampler, NULL, out, samples, count, workers,
            JOB_RANK);
}

int passlit_sampler_batch(const struct passlit_sampler *sampler,
        uint64_t offset, size_t count, size_t workers,
        passlit_emit emit, void *context) {
    char *output;
    size_t i, stride;
    int status = PASSLIT_OK;

    if (sampler == NULL || emit == NULL || count == 0) {
        return PASSLIT_INVALID;
    }
    if (offset > UINT64_MAX - sampler->index ||
            count - 1 > UINT64_MAX - sampler->index - offset) {
        return PASSLIT_RANGE;
    }
    if (workers == 0) {
        workers = passlit_worker_count();
    }
    stride = passlit_sampler_length(sampler) + 1;
#ifdef PASSLIT_THREADS
    if (workers > 1 && count >= PASSLIT_PARALLEL_MIN_SAMPLES) {
        return parallel_samples(sampler, offset, stride, count, workers,
                emit, context);
    }
#else
    if (workers != 1) {
        return PASSLIT_INVALID;
    }
#endif
    output = malloc(stride);
    if (output == NULL) {
        return PASSLIT_MEMORY;
    }
    for (i = 0; i < count; ++i) {
        status = passlit_sampler_sample(output, stride, sampler,
                offset + (uint64_t)(i));
        if (status == PASSLIT_OK) {
            status = emit(output, context);
        }
        if (status != PASSLIT_OK) {
            break;
        }
    }
    free(output);
    return status;
}
