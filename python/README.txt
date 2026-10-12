CPython 3.13+, abi3, ordinary GIL builds. Cython is build-only.

    import passlit
    passlit.generate()

Arguments, positional or keyword:

    groups=None, custom=None, size_kind=PASSLIT_BITS_DOWN, size_val=128,
    seed=None, sample_index=0, patterns=1, count=1, workers=0

generate() returns a list of strings.

rank(samples, ...) takes strings and returns a list of integer ranks.
Invalid samples get -1; wrong types raise TypeError.

encode(ranks, ...) takes integers and returns a list of strings.
Invalid ranks raise ValueError.

rank() and encode() take groups, custom, size_kind, size_val, patterns,
workers. Defaults match generate(); share settings with **kwargs.

test(groups, custom, size_kind, size_val, patterns) returns a dict:
length, scope (exact integer), entropy_bits (floor, ceiling).

Size modes: PASSLIT_LENGTH, PASSLIT_BITS_UP, PASSLIT_BITS_DOWN.
Patterns: 0 disables, nonzero enables.
String settings: str or None. Strings use C termination rules.

Workers 0: half the online CPUs, minimum one; THREADS=0 stays serial.
Native operations release the GIL.
Invalid settings raise ValueError; random failures raise OSError.

    make -C python test
    python3 -m pip wheel ./python

Flags: config.mk. Source builds link system GMP/libcrypto; release wheels
embed them and use system glibc. pip does not check CPU requirements.

For CPU architectures other than x86-64-v3:

    ARCH_FLAGS="" pip install --no-binary=passlit passlit
