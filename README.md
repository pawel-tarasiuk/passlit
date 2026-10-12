# passlit 0.0.1

utility to create passwords and codes that feel good to read, type and print.

C99 library and tool. Exact GMP counts, uniform sampling. OpenSSL randomness.

## Build

GCC with `-fhardened`, GNU Make 4.3+, GMP and OpenSSL headers and libraries.
Settings: `config.mk`. Clean before changing them.

```sh
make
make test
make DEBUG=2  # separate debug symbols; DEBUG=1 keeps them embedded
make STATIC=1  # static PIE; needs static dependencies
make THREADS=0  # disable pthread workers
make install PREFIX=/usr  # supports DESTDIR and directory overrides
make archive
```

Installs the tool, library, header, man pages, licenses and `passlit.pc`.
Use `pkg-config --cflags --libs passlit` for linking.
Release artifacts: [release/README.md](release/README.md).

## Use

```sh
./passlit  # 128 bits, rounded down
./passlit -a 'U*L*D*O*' -l 22 -n 1000
./passlit --seed example -n 10  # reproducible, not secret
./passlit --help
```

Groups: U/L/D/O for standard uppercase/lowercase/digits/punctuation;
u/l/d/o for full ASCII groups; c for custom characters. Omit to exclude,
append * to require. Default: U*L*D*O*.

Patterns are restricted by default: at most three identical characters or
four from one group consecutively; no adjacent repeated bigrams or trigraphs.
Use `-p` to allow patterns. Details: `man ./man/passlit.1`.

`-e` reads whitespace-separated stdin ranks and writes one sample per line.
`-r` reverses it, also reading whitespace-separated inputs.
Use `-x` for padded `0x` ranks.
`-t` reports exact scope size and entropy bounds.
The library exposes the same conversions and exact counts; see `passlit(3)`.

## Library

```c
#include "passlit.h"

char sample[128];
struct passlit_config config = PASSLIT_DEFAULT_CONFIG;
int status = passlit_generate(sample, sizeof(sample), &config);
```

API: `passlit.h`. `passlit_configure()` packs settings into a config.
Suffix `2` takes separate settings; `p` allocates output
for the caller to `free()`. Set `config.count` (default 1) for a packed batch:
`sample\0sample\0\0`. Storage is `count * (length + 1) + 1` bytes;
count zero returns one zero byte. Separate-argument forms end with count
and workers.
Set `config.workers` (default 0: automatic) to use optional batch threads.
Counts are prepared once per batch. API manual: `man ./man/passlit.3`.
Python 3.13+: [python/README.txt](python/README.txt).

## Checks

```sh
make sanitize
make memcheck  # optional Valgrind; DEBUGINFOD=1 allows symbol downloads
make stress  # about 1.7 GiB
```

`make clean` removes build outputs; `make distclean` also removes packaging
outputs and source archives.

## License

Copyright (C) 2026 Pawe\l{} Tarasiuk <pablo.t89@gmail.com>.
LGPL version 3 only; see [LICENSE.md](LICENSE.md).
Developed with assistance from OpenAI models.
