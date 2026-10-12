# cython: language_level=3, c_string_type=unicode, c_string_encoding=utf8

"""Create passwords and codes that feel good to read, type and print."""

from libc.stdlib cimport calloc, free
from libc.stdint cimport uint64_t
from libc.string cimport strlen

from cpython.bytes cimport PyBytes_AsString, PyBytes_FromStringAndSize


cdef extern from "gmp.h" nogil:
    ctypedef struct __mpz_struct:
        pass
    ctypedef __mpz_struct mpz_t[1]
    void mpz_init(mpz_t)
    void mpz_clear(mpz_t)
    int mpz_sgn(const mpz_t)
    void mpz_import(mpz_t, size_t, int, size_t, int, size_t, const void *)
    void *mpz_export(void *, size_t *, int, size_t, int, size_t, const mpz_t)
    size_t mpz_sizeinbase(const mpz_t, int)


cdef extern from "passlit.h" nogil:
    cdef enum passlit_size_kind:
        pass

    cpdef enum:
        PASSLIT_LENGTH
        PASSLIT_BITS_UP
        PASSLIT_BITS_DOWN

    enum:
        PASSLIT_MEMORY
        PASSLIT_RANDOM
        PASSLIT_RANGE

    int passlit_generatep2(
        char **, const char *, const char *, passlit_size_kind, size_t,
        const char *, uint64_t, int, size_t, size_t
    )
    int passlit_scope2(
        mpz_t, size_t *, const char *, const char *, passlit_size_kind,
        size_t, int
    )
    int passlit_encodep2(
        char **, mpz_t *, const char *, const char *, passlit_size_kind,
        size_t, int, size_t, size_t
    )
    int passlit_rankp2(
        mpz_t **, const char *const *, const char *, const char *,
        passlit_size_kind, size_t, int, size_t, size_t
    )


cdef void __status_check(int status) except *:
    if status == PASSLIT_MEMORY:
        raise MemoryError()
    if status == PASSLIT_RANDOM:
        raise OSError("Secure randomness failed")
    if status:
        raise ValueError("Invalid input or infeasible sampling configuration")


cdef list __long_str_to_list(char *output, size_t count):
    cdef size_t length
    if count == 0 or output[0] == '\0':
        return []
    length = count * (strlen(output) + 1) - 1
    return (<bytes>output[:length]).decode("utf8").split("\0")


cdef object __mpz_t_to_py(const mpz_t value):
    cdef int sign = mpz_sgn(value)
    cdef bytes data
    if sign == 0:
        return 0
    data = PyBytes_FromStringAndSize(
        NULL, (mpz_sizeinbase(value, 2) + 7) // 8
    )
    mpz_export(PyBytes_AsString(data), NULL, -1, 1, 0, 0, value)
    result = int.from_bytes(data, "little")
    return -result if sign < 0 else result


def generate(
    str groups=None,
    str custom=None,
    passlit_size_kind size_kind=<passlit_size_kind>PASSLIT_BITS_DOWN,
    size_t size_val=128,
    str seed=None,
    uint64_t sample_index=0,
    int patterns=1,
    size_t count=1,
    size_t workers=0,
):
    """Return samples in order; workers=0 selects automatic parallelism."""
    cdef const char *groups_ptr = NULL
    cdef const char *custom_ptr = NULL
    cdef const char *seed_ptr = NULL
    cdef char *output = NULL
    cdef int status
    if seed is not None:
        seed_ptr = seed
    if groups is not None:
        groups_ptr = groups
    if custom is not None:
        custom_ptr = custom
    try:
        with nogil:
            status = passlit_generatep2(
                &output, groups_ptr, custom_ptr, size_kind, size_val,
                seed_ptr, sample_index, patterns, count, workers
            )
        __status_check(status)
        return __long_str_to_list(output, count)
    finally:
        free(output)


def test(
    str groups=None,
    str custom=None,
    passlit_size_kind size_kind=<passlit_size_kind>PASSLIT_BITS_DOWN,
    size_t size_val=128,
    int patterns=1,
):
    """Return length, scope and (floor, ceiling) entropy_bits."""
    cdef const char *groups_ptr = NULL
    cdef const char *custom_ptr = NULL
    cdef mpz_t total
    cdef size_t length
    cdef int status
    if groups is not None:
        groups_ptr = groups
    if custom is not None:
        custom_ptr = custom
    mpz_init(total)
    try:
        with nogil:
            status = passlit_scope2(
                total, &length, groups_ptr, custom_ptr, size_kind,
                size_val, patterns
            )
        __status_check(status)
        scope = __mpz_t_to_py(total)
        lower = scope.bit_length() - 1
        upper = (scope - 1).bit_length()
        return dict(length=length, scope=scope, entropy_bits=(lower, upper))
    finally:
        mpz_clear(total)


def encode(
    ranks,
    str groups=None,
    str custom=None,
    passlit_size_kind size_kind=<passlit_size_kind>PASSLIT_BITS_DOWN,
    size_t size_val=128,
    int patterns=1,
    size_t workers=0,
):
    """Return samples for zero-based integer ranks; invalid ranks raise."""
    ranks = tuple(ranks)
    cdef size_t count = len(ranks), initialized = 0, i
    cdef const char *groups_ptr = NULL
    cdef const char *custom_ptr = NULL
    cdef mpz_t *values = NULL
    cdef char *output = NULL
    cdef bytes data
    cdef int status
    if groups is not None:
        groups_ptr = groups
    if custom is not None:
        custom_ptr = custom
    if count:
        values = <mpz_t *>calloc(count, sizeof(mpz_t))
        if values == NULL:
            raise MemoryError()
    try:
        for i in range(count):
            if not isinstance(ranks[i], int):
                raise TypeError("Ranks must be integers")
            if ranks[i] < 0:
                raise ValueError("Ranks must be nonnegative")
            data = ranks[i].to_bytes(
                (ranks[i].bit_length() + 7) // 8, "little"
            )
            mpz_init(values[i])
            initialized += 1
            mpz_import(
                values[i], len(data), -1, 1, 0, 0, <const char *>data
            )
        with nogil:
            status = passlit_encodep2(
                &output, values, groups_ptr, custom_ptr, size_kind,
                size_val, patterns, count, workers
            )
        __status_check(status)
        return __long_str_to_list(output, count)
    finally:
        free(output)
        for i in range(initialized):
            mpz_clear(values[i])
        free(values)


def rank(
    samples,
    str groups=None,
    str custom=None,
    passlit_size_kind size_kind=<passlit_size_kind>PASSLIT_BITS_DOWN,
    size_t size_val=128,
    int patterns=1,
    size_t workers=0,
):
    """Rank strings; invalid samples get -1, wrong types raise TypeError."""
    samples = tuple(samples)
    cdef size_t count = len(samples), i
    cdef const char *groups_ptr = NULL
    cdef const char *custom_ptr = NULL
    cdef const char **strings = NULL
    cdef mpz_t *values = NULL
    cdef str sample
    cdef int status
    if groups is not None:
        groups_ptr = groups
    if custom is not None:
        custom_ptr = custom
    if count:
        strings = <const char **>calloc(count, sizeof(char *))
        if strings == NULL:
            raise MemoryError()
    try:
        for i in range(count):
            sample = samples[i]
            if sample is None:
                raise TypeError("Samples must be strings")
            strings[i] = sample
        with nogil:
            status = passlit_rankp2(
                &values, strings, groups_ptr, custom_ptr, size_kind,
                size_val, patterns, count, workers
            )
        if status != PASSLIT_RANGE or values == NULL:
            __status_check(status)
        return [__mpz_t_to_py(values[i]) for i in range(count)]
    finally:
        if values != NULL:
            for i in range(count):
                mpz_clear(values[i])
        free(values)
        free(strings)
