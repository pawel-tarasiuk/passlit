/* Licensed under the GNU Lesser General Public License, version 3 only. */

#define _POSIX_C_SOURCE 200809L

#include "passlit.h"
#include "platform.h"
#include "sampling.h"

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>

#include <getopt.h>
#include <unistd.h>


enum operation {
    OP_GENERATE,
    OP_ENCODE,
    OP_RANK,
    OP_TEST
};

static int show_scope(const struct passlit_sampler *sampler) {
    mpz_srcptr total = passlit_sampler_count(sampler);
    size_t bits = mpz_sizeinbase(total, 2) - 1;
    printf("length: %zu\n", passlit_sampler_length(sampler));
    gmp_printf("scope: %Zd\n", total);
    printf("entropy_bits: %zu %zu\n",
            bits, bits + (mpz_popcount(total) != 1));
    return ferror(stdout) ? PASSLIT_RANDOM : PASSLIT_OK;
}

/* Drain overlong records before reporting failure. */
static int read_record(char *token, size_t capacity) {
    size_t used = 0;
    int value, valid = 1, seen = 0;
    while ((value = fgetc(stdin)) != EOF) {
        if (isspace(value)) {
            if (!seen) {
                continue;
            }
            break;
        }
        seen = 1;
        if (value == '\0' || used == capacity - 1) {
            valid = 0;
        } else {
            token[used++] = (char)(value);
        }
    }
    if (ferror(stdin)) {
        return -1;
    }
    if (value == EOF && !seen) {
        return 0;
    }
    token[used] = '\0';
    return valid ? 1 : 2;
}

static void parse_rank(mpz_t rank, char *value, int base, int valid) {
    size_t i;
    if (base == 16 && value[0] == '0' &&
            (value[1] == 'x' || value[1] == 'X')) {
        value += 2;
    } else if (base == 16 && (value[0] == 'x' || value[0] == 'X')) {
        ++value;
    }
    valid = valid && *value != '\0';
    for (i = 0; value[i] != '\0' && valid; ++i) {
        valid = strchr(base == 16 ? "0123456789abcdefABCDEF" :
                "0123456789", value[i]) != NULL;
    }
    if (!valid || mpz_set_str(rank, value, base) != 0) {
        mpz_set_si(rank, -1);
    }
}

static void print_rank(mpz_srcptr rank, int base, char *digits, size_t width) {
    size_t i;
    if (mpz_sgn(rank) >= 0 && base == 16) {
        mpz_get_str(digits, 16, rank);
        fputs("0x", stdout);
        for (i = strlen(digits); i < width; ++i) {
            putchar('0');
        }
        puts(digits);
    } else {
        gmp_printf("%Zd\n", rank);
    }
}

static int convert(const struct passlit_sampler *sampler,
        enum operation mode, int base, size_t workers) {
    enum { BATCH_RECORDS = 1024, BATCH_BYTES = 8 * 1024 * 1024 };
    mpz_t maximum;
    mpz_t *ranks;
    const char **samples;
    char *tokens, *output, *digits;
    size_t capacity, stride = passlit_sampler_length(sampler) + 1;
    size_t width, i, count, limit, initialized = 0, record_number = 0;
    int record = 1, valid, status = PASSLIT_OK, converted;

    mpz_init(maximum);
    mpz_sub_ui(maximum, passlit_sampler_count(sampler), 1);
    width = mpz_sizeinbase(maximum, 16);
    capacity = mode == OP_RANK ? stride + 1 :
        mpz_sizeinbase(maximum, base) + 4;
    limit = BATCH_BYTES / (capacity + stride + sizeof(*ranks) +
            sizeof(*samples));
    if (limit > BATCH_RECORDS) {
        limit = BATCH_RECORDS;
    }
    if (limit == 0) {
        limit = 1;
    }
    tokens = malloc(limit * capacity);
    output = malloc(limit * stride);
    ranks = malloc(limit * sizeof(*ranks));
    samples = malloc(limit * sizeof(*samples));
    digits = malloc(mpz_sizeinbase(maximum, 10) + 2);
    if (tokens == NULL || output == NULL || ranks == NULL ||
            samples == NULL || digits == NULL) {
        status = PASSLIT_MEMORY;
        goto done;
    }
    for (; initialized < limit; ++initialized) {
        mpz_init(ranks[initialized]);
    }
    while (record > 0) {
        for (count = 0; count < limit; ++count) {
            record = read_record(tokens + count * capacity, capacity);
            if (record <= 0) {
                break;
            }
            samples[count] = record == 1 ? tokens + count * capacity : NULL;
            if (mode == OP_ENCODE) {
                parse_rank(ranks[count], tokens + count * capacity,
                        base, record == 1);
            }
        }
        if (mode == OP_ENCODE) {
            converted = passlit_sampler_encode_batch(sampler, output,
                    ranks, count, workers);
        } else {
            converted = passlit_sampler_rank_batch(sampler, ranks,
                    samples, count, workers);
        }
        if (converted != PASSLIT_OK && converted != PASSLIT_RANGE) {
            status = converted;
            break;
        }
        for (i = 0; i < count; ++i) {
            ++record_number;
            if (mode == OP_ENCODE) {
                valid = output[i * stride] != '\0';
                puts(output + i * stride);
            } else {
                valid = mpz_sgn(ranks[i]) >= 0;
                print_rank(ranks[i], base, digits, width);
            }
            if (!valid) {
                fprintf(stderr, "passlit: invalid input at token %zu\n",
                        record_number);
                status = PASSLIT_RANGE;
            }
        }
        if (ferror(stdout)) {
            status = PASSLIT_RANDOM;
            break;
        }
    }
    if (record < 0) {
        status = PASSLIT_RANDOM;
    }
done:
    for (i = 0; i < initialized; ++i) {
        mpz_clear(ranks[i]);
    }
    free(ranks);
    free(samples);
    free(tokens);
    free(output);
    free(digits);
    mpz_clear(maximum);
    return status;
}

static int print_sample(const char *sample, void *context) {
    FILE *stream = context;
    return fprintf(stream, "%s\n", sample) < 0 ? PASSLIT_RANDOM : PASSLIT_OK;
}

static void usage(FILE *stream) {
    fputs("Usage: passlit [OPTIONS]\n"
            "  -g, --generate           Generate samples (default)\n"
            "  -e, --encode             Read ranks from stdin; emit samples\n"
            "  -r, --rank               Read samples from stdin; emit ranks\n"
            "  -t, --test               Validate and describe the scope\n"
            "  -d, --decimal            Decimal ranks (default)\n"
            "  -x, --hex                Hex ranks, padded with 0x prefix\n"
            "  -a, --alphabet SET       U/u L/l D/d O/o c; suffix * requires "
            "group\n"
            "  -c, --custom POOL        Non-whitespace byte pool for c/c*\n"
            "  -s, --seed TEXT          Reproducible samples (not additional "
            "randomness)\n"
            "  -l, --length N           Explicit sample length\n"
            "  -b, --bits N             Exact-space strength target (default "
            "128 "
            "bits)\n"
            "  -R, --round MODE         Round down/0 (default) or up/1 for "
            "bits\n"
            "  -n, --number N           Number of samples (default: 1)\n"
            "  -j, --jobs N             Workers (0: auto; batches >= 256; "
            "thread build)\n"
            "  -P, --reject-patterns    Enable pattern rules (default)\n"
            "  -p, --allow-patterns     Disable pattern rules\n"
            "  -V, --version            Show version\n"
            "  -h, --help               Show this help\n",
            stream);
    fprintf(stream,
            "  Maximum length: %zu (page size clamped to 4096..65536); "
            "default alphabet U*L*D*O*\n",
            passlit_length_limit());
}

static int number(const char *text, size_t *value) {
    uintmax_t parsed;
    char *end;
    if (*text < '0' || *text > '9') {
        return 0;
    }
    errno = 0;
    parsed = strtoumax(text, &end, 10);
    if (errno != 0 || *end != '\0' || parsed > SIZE_MAX) {
        return 0;
    }
    *value = (size_t)(parsed);
    return 1;
}

int main(int argc, char **argv) {
    static char input_buffer[64 * 1024], output_buffer[64 * 1024];
    static const struct option options[] = {
        { "generate", no_argument, NULL, 'g' },
        { "encode", no_argument, NULL, 'e' },
        { "rank", no_argument, NULL, 'r' },
        { "test", no_argument, NULL, 't' },
        { "decimal", no_argument, NULL, 'd' },
        { "hex", no_argument, NULL, 'x' },
        { "alphabet", required_argument, NULL, 'a' },
        { "custom", required_argument, NULL, 'c' },
        { "seed", required_argument, NULL, 's' },
        { "length", required_argument, NULL, 'l' },
        { "bits", required_argument, NULL, 'b' },
        { "round", required_argument, NULL, 'R' },
        { "number", required_argument, NULL, 'n' },
        { "jobs", required_argument, NULL, 'j' },
        { "reject-patterns", no_argument, NULL, 'P' },
        { "allow-patterns", no_argument, NULL, 'p' },
        { "version", no_argument, NULL, 'V' },
        { "help", no_argument, NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };
    struct passlit_config config = PASSLIT_DEFAULT_CONFIG;
    struct passlit_sampler *sampler;
    size_t count = 1, minimum = 1, requested, workers = 0;
    const char *message, *selector;
    enum operation mode = OP_GENERATE;
    int mode_seen = 0, base = 10, format_seen = 0;
    int status, option, length_seen = 0, bits_seen = 0, round_seen = 0,
                        round_down = 1, singleton = 0;
    if (!isatty(STDIN_FILENO)) {
        (void)setvbuf(stdin, input_buffer, _IOFBF, sizeof(input_buffer));
    }
    if (!isatty(STDOUT_FILENO)) {
        (void)setvbuf(stdout, output_buffer, _IOFBF, sizeof(output_buffer));
    }
    while ((option = getopt_long(argc, argv, "a:c:s:l:b:R:n:j:hVPpgertdx",
            options,
            NULL)) != -1) {
        switch (option) {
        case 'g':
        case 'e':
        case 'r':
        case 't':
            if (mode_seen != 0 && mode_seen != option) {
                goto invalid;
            }
            mode_seen = option;
            switch (option) {
            case 'g': mode = OP_GENERATE; break;
            case 'e': mode = OP_ENCODE; break;
            case 'r': mode = OP_RANK; break;
            default: mode = OP_TEST; break;
            }
            break;
        case 'd':
        case 'x':
            format_seen = 1;
            base = option == 'd' ? 10 : 16;
            break;
        case 'a':
            config.groups = optarg;
            break;
        case 'c':
            config.custom = optarg;
            break;
        case 's':
            config.seed = optarg;
            break;
        case 'l':
        case 'b':
            if (!number(optarg, &config.size_val)) {
                goto invalid;
            }
            length_seen |= option == 'l';
            bits_seen |= option == 'b';
            break;
        case 'R':
            round_seen = 1;
            if (strcmp(optarg, "down") == 0 || strcmp(optarg, "0") == 0) {
                round_down = 1;
            } else if (strcmp(optarg, "up") == 0 || strcmp(optarg, "1") == 0) {
                round_down = 0;
            } else {
                goto invalid;
            }
            break;
        case 'n':
            if (!number(optarg, &count) || count == 0) {
                goto invalid;
            }
            break;
        case 'V':
            printf("passlit %u.%u.%u\n", PASSLIT_VERSION >> 24,
                    (PASSLIT_VERSION >> 16) & 0xffU,
                    (PASSLIT_VERSION >> 8) & 0xffU);
            return PASSLIT_OK;
        case 'j':
            if (!number(optarg, &workers)) {
                goto invalid;
            }
            break;
        case 'P':
            config.patterns = 1;
            break;
        case 'p':
            config.patterns = 0;
            break;
        case 'h':
            usage(stdout);
            return PASSLIT_OK;
        default:
            goto invalid;
        }
    }
    if (optind != argc || (length_seen && bits_seen) ||
            (round_seen && length_seen) ||
            (format_seen && mode != OP_ENCODE && mode != OP_RANK)) {
        goto invalid;
    }
    if (length_seen) {
        config.size_kind = PASSLIT_LENGTH;
    } else if (round_down) {
        config.size_kind = PASSLIT_BITS_DOWN;
    } else {
        config.size_kind = PASSLIT_BITS_UP;
    }
#ifndef PASSLIT_THREADS
    if (workers > 1) {
        fputs("passlit: built without thread support\n", stderr);
        return PASSLIT_INVALID;
    }
#endif
    requested = config.size_val;
    if (config.patterns && config.groups != NULL &&
            (strcmp(config.groups, "c") == 0 ||
            strcmp(config.groups, "c*") == 0) && config.custom != NULL &&
            *config.custom != '\0' && length_seen && config.size_val >= 4) {
        for (selector = config.custom; *selector == *config.custom;
                ++selector) {
        }
        singleton = *selector == '\0';
    }
    if (length_seen && mode == OP_GENERATE) {
        minimum = 0;
        selector = config.groups != NULL ? config.groups : "U*L*D*O*";
        for (; *selector != '\0'; ++selector) {
            minimum += *selector == '*' ? 1U : 0U;
        }
        if (minimum == 0) {
            minimum = 1;
        }
        if (config.size_val < minimum) {
            config.size_val = minimum;
        }
    }
    status = passlit_sampler_create(&sampler, &config);
    if (status != PASSLIT_OK) {
        goto failed;
    }
    if (mode == OP_TEST) {
        status = show_scope(sampler);
    } else if (mode == OP_GENERATE) {
        status = passlit_sampler_batch(sampler, 0, count, workers,
                print_sample, stdout);
    } else {
        status = convert(sampler, mode, base, workers);
    }
    passlit_sampler_free(sampler);
    if ((mode == OP_ENCODE || mode == OP_RANK) &&
            status == PASSLIT_RANGE) {
        return fflush(stdout) == 0 ? status : PASSLIT_RANDOM;
    }
    if (status != PASSLIT_OK) {
        goto failed;
    }
    if (mode == OP_GENERATE && length_seen && requested < minimum) {
        fprintf(stderr, "passlit: length %zu increased to %zu\n",
                requested, minimum);
    }
    if (fflush(stdout) != 0) {
        return PASSLIT_RANDOM;
    }
    if (singleton) {
        fputs("passlit: pattern rules relaxed for a singleton alphabet\n",
                stderr);
    }
    return singleton || (mode == OP_GENERATE &&
        length_seen && requested < minimum) ?
        PASSLIT_RANGE : PASSLIT_OK;
failed:
    message = "invalid configuration";
    if (status == PASSLIT_RANGE) {
        message = "no supported length fits, impossible constraints, "
            "or buffer too small";
    } else if (status == PASSLIT_RANDOM) {
        message = "secure random source failed";
    } else if (status == PASSLIT_MEMORY) {
        message = "allocation failed";
    }
    fprintf(stderr, "passlit: %s\n", message);
    return status;
invalid:
    usage(stderr);
    return PASSLIT_INVALID;
}
