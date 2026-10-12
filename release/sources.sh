#!/bin/bash

# Versions, archives and SHA256 hashes.
declare -gA ARCHIVES VERSIONS HASHES URLS

source_archive() {
    VERSIONS["$1"]="$VERSION"
    ARCHIVES["$1"]="${1}-${VERSION}.${2}"
    HASHES["$1"]="$3"
    URLS["$1"]="${4:-https://mirrors.kernel.org/gnu/$1/${ARCHIVES[$1]}}"
}

CYTHON_REQUIREMENT="Cython>=3.2,<4"

VERSION="3.13.16"
source_archive "Python" "tar.xz" \
    "f4b1bfb3c79b5bb11b8d228a12504163b4c0dab4d679828d8f5f26b6cb6ab35d" \
    "https://www.python.org/ftp/python/${VERSION}/Python-${VERSION}.tar.xz"

VERSION="2.6"
URL="https://github.com/amadvance/advancecomp/releases/download"
source_archive "advancecomp" "tar.gz" \
    "b07d77735540409771cbe1b6df165b5151c11bb9c3d8f01290be0ec88ec3498f" \
    "${URL}/v${VERSION}/advancecomp-${VERSION}.tar.gz"

VERSION="2.47"
source_archive "binutils" "tar.xz" \
    "154ab23b60070e8f27013c22977f1129425d67d1e8acd6e13010e617811e4cff"

VERSION="16.2.0"
source_archive "gcc" "tar.xz" \
    "e6738e29597f733270731aa90600f37ffdc045079dfc27ec7e8192cc81085c3e" \
    "https://mirrors.kernel.org/gnu/gcc/gcc-${VERSION}/gcc-${VERSION}.tar.xz"

VERSION="2.39"
REVISION="ce65d944e38a20cb70af2a48a4b8aa5d8fabe1cc"
source_archive "glibc" "tar.gz" \
    "45280517ec6b1c7e84f142f04caa206e4a31ddf23386ebec26f23cd0406ef99b" \
    "https://github.com/bminor/glibc/archive/${REVISION}.tar.gz"

VERSION="6.2.1"
source_archive "gmp" "tar.xz" \
    "fd4829912cddd12f84181c3451cc752be224643e87fac497b69edddadc49b4f2"

VERSION="6.12.112"
URL="https://cdn.kernel.org/pub/linux/kernel/v${VERSION%%.*}.x"
source_archive "linux" "tar.xz" \
    "164dc9d1f6c93c61a15e1f071c48379b467f2b17c469cce7223471968208ed03" \
    "${URL}/linux-${VERSION}.tar.xz"

VERSION="1.3.1"
source_archive "mpc" "tar.gz" \
    "ab642492f5cf882b74aa0cb730cd410a81edcdbec895183ce930e706c1c759b8"

VERSION="4.2.2"
source_archive "mpfr" "tar.xz" \
    "b67ba0383ef7e8a8563734e2e889ef5ec3c3b898a01d00fa0a6869ad81c6ce01"

VERSION="3.5.9"
URL="https://github.com/openssl/openssl/releases/download"
source_archive "openssl" "tar.gz" \
    "603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a" \
    "${URL}/openssl-${VERSION}/openssl-${VERSION}.tar.gz"

VERSION="d28b750767d440d9ecabac0fef487eb8baf86a08"
source_archive "pyenv" "tar.gz" \
    "cb34e7d58fe4f333c5e1a7c6c731dad618707cff01d4a756fa46b73cf3ca3aba" \
    "https://codeload.github.com/pyenv/pyenv/tar.gz/${VERSION}"

VERSION="329b1dd18c41c75609a48959b5c21e95dc535f34"
source_archive "zopfli" "tar.gz" \
    "dae16318979f170525d5e3700ce08c3e11068618f05f6ccd0edd54a842fe5058" \
    "https://codeload.github.com/pawel-tarasiuk/zopfli/tar.gz/${VERSION}"

KERNEL_MIN="${VERSIONS[linux]%.*}"
