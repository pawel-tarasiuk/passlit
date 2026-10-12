#!/bin/bash

set -euo pipefail

compiler() {
    local COMPILER="$1" ARG SEED="link"
    local ROOT="/var/tmp/build/passlit" DATE TIME
    local EXTRA=() LINK_OPTIONS=()
    shift

    case "$COMPILER" in
        target|packaging)
            if [[ "$COMPILER" == "packaging" ]]; then
                EXTRA=(--sysroot=/ -B/usr/lib/x86_64-linux-gnu/
                    -isystem /usr/include/x86_64-linux-gnu)
                for ARG in "$@"; do
                    if [[ "$ARG" == */getbuildinfo.c ]]; then
                        DATE="$(date -u -d "@${SOURCE_DATE_EPOCH}" \
                            "+%b %e %Y")"
                        TIME="$(date -u -d "@${SOURCE_DATE_EPOCH}" \
                            "+%H:%M:%S")"
                        EXTRA+=("-DDATE=\"${DATE}\"" "-DTIME=\"${TIME}\"")
                    fi
                done
            fi
            COMPILER="${ROOT}/toolchain/usr/bin/x86_64-release-linux-gnu-gcc"
            ;;
    esac
    for ARG in "$@"; do
        case "$ARG" in
            *.c|*.cc|*.cpp|*.S) SEED="${PWD}/${ARG}" ;;
            -r|-Wl,-r) LINK_OPTIONS=("-Wl,--no-gc-sections") ;;
        esac
    done
    exec "$COMPILER" "-frandom-seed=${SEED}" \
        "-ffile-prefix-map=${ROOT}=/usr/local/src/passlit/build" \
        -Werror=date-time "${EXTRA[@]}" "$@" "${LINK_OPTIONS[@]}"
}

environment() {
    # shellcheck source=release/sources.sh
    source "$(dirname -- "${BASH_SOURCE[0]}")/sources.sh"
    SOURCE="/usr/local/src/passlit"
    BUILD="/var/tmp/build/passlit"
    TARGET="x86_64-release-linux-gnu"
    SYSROOT="${BUILD}/toolchain/sysroot"
    TOOLS="/opt/passlit-packaging"
    JOBS="${JOBS:-8}"
    export SOURCE_DATE_EPOCH=1791504000 LC_ALL="C" TZ="UTC" PYTHONHASHSEED=0
    export ARCH_FLAGS="-march=x86-64-v3 -mshstk"
    export OPT_FLAGS="-Os -fvect-cost-model=cheap"
    FLAGS="${OPT_FLAGS} -g2 ${ARCH_FLAGS} -fhardened -fno-lto"
    FLAGS+=" -ffunction-sections -fdata-sections"
    LINK_FLAGS="-Wl,-z,relro,-z,now,--hash-style=both,--gc-sections -fno-lto"
    export PATH="${BUILD}/toolchain/usr/bin:${TOOLS}/python/bin"
    PATH+=":${TOOLS}/bin:/usr/bin:/bin"
    unset CFLAGS CXXFLAGS CPPFLAGS LDFLAGS LDLIBS MAKEFLAGS MFLAGS
    umask 022
}

extract() {
    mkdir -p -- "$2"
    tar --no-same-owner -xf "/downloads/$1" -C "$2" --strip-components=1
}

toolchain() (
    environment

    ROOT="${BUILD}/toolchain"
    PREFIX="${ROOT}/usr"
    export CC="/usr/local/src/passlit/release/build.sh compiler gcc"
    export CXX="/usr/local/src/passlit/release/build.sh compiler g++"
    export CFLAGS_FOR_BUILD="$FLAGS" CXXFLAGS_FOR_BUILD="$FLAGS"
    export CFLAGS_FOR_TARGET="${FLAGS} -frandom-seed=passlit-runtime"
    CFLAGS_FOR_TARGET+=" -ffile-prefix-map=${ROOT}="
    CFLAGS_FOR_TARGET+="/usr/local/src/passlit/build"
    export CXXFLAGS_FOR_TARGET="$CFLAGS_FOR_TARGET"
    export LDFLAGS_FOR_TARGET="$LINK_FLAGS" LDFLAGS_FOR_BUILD="$LINK_FLAGS"

    mkdir -p -- "$PREFIX" "${SYSROOT}/usr/lib" "${SYSROOT}/lib"

    if [[ ! -f "${ROOT}/headers.done" ]]; then
        extract "${ARCHIVES[linux]}" "${ROOT}/linux"
        make -C "${ROOT}/linux" headers_install ARCH=x86 \
            INSTALL_HDR_PATH="${SYSROOT}/usr"
        touch -- "${ROOT}/headers.done"
    fi

    if [[ ! -f "${ROOT}/binutils-hardened.done" ]]; then
        extract "${ARCHIVES[binutils]}" "${ROOT}/binutils"
        mkdir -p -- "${ROOT}/binutils-hardened-build"
        (
            cd -- "${ROOT}/binutils-hardened-build"
            export CFLAGS="$FLAGS" CXXFLAGS="$FLAGS" LDFLAGS="$LINK_FLAGS"
            ../binutils/configure --prefix=/usr --target="$TARGET" \
                --with-sysroot="$SYSROOT" --disable-nls --disable-werror \
                --enable-default-hash-style=both
            make -j "$JOBS"
            make DESTDIR="$ROOT" install
        )
        touch -- "${ROOT}/binutils-hardened.done"
    fi

    # Bootstrap libc with Debian's compiler; rebuild with ours below.
    libc() {
        local COMPILER="$1" BUILD="$2"

        mkdir -p -- "${ROOT}/${BUILD}"
        (
            cd -- "${ROOT}/${BUILD}"
            export CFLAGS="${OPT_FLAGS} -g2 ${ARCH_FLAGS}"
            CFLAGS+=" -ffunction-sections -fdata-sections"
            export LDFLAGS="-Wl,--gc-sections"
            CC="$COMPILER" ../glibc/configure --prefix=/usr \
                --with-headers="${SYSROOT}/usr/include" \
                --enable-kernel="$KERNEL_MIN" \
                --enable-static-pie --disable-werror
            printf "%s\n" "LDFLAGS.so += -Wl,--gc-sections" \
                "LDFLAGS.so += -Wl,--undefined=__x86_cacheinfo_p" \
                "LDFLAGS-rtld += -Wl,--gc-sections" > configparms
            make -j "$JOBS"
            make install_root="$SYSROOT" install
        )
    }

    if [[ ! -f "${ROOT}/libc-bootstrap.done" ]]; then
        extract "${ARCHIVES[glibc]}" "${ROOT}/glibc"
        libc "$CC" "glibc-bootstrap"
        touch -- "${ROOT}/libc-bootstrap.done"
    fi

    if [[ ! -f "${ROOT}/gcc-hardened.done" ]]; then
        extract "${ARCHIVES[gcc]}" "${ROOT}/gcc"
        extract "${ARCHIVES[gmp]}" "${ROOT}/gcc/gmp"
        extract "${ARCHIVES[mpfr]}" "${ROOT}/gcc/mpfr"
        extract "${ARCHIVES[mpc]}" "${ROOT}/gcc/mpc"
        mkdir -p -- "${ROOT}/gcc-hardened-build"
        (
            cd -- "${ROOT}/gcc-hardened-build"
            export CFLAGS="$FLAGS" CXXFLAGS="$FLAGS" LDFLAGS="$LINK_FLAGS"
            ../gcc/configure --prefix=/usr --target="$TARGET" \
                --with-sysroot="$SYSROOT" --enable-languages=c \
                --disable-bootstrap --disable-multilib --disable-nls \
                --disable-libgomp --disable-libquadmath --disable-libstdcxx \
                --disable-libvtv --disable-libssp --disable-lto \
                --enable-default-pie --enable-default-ssp \
                --enable-host-pie --enable-host-bind-now
            make -j "$JOBS"
            make DESTDIR="$ROOT" install
        )
        touch -- "${ROOT}/gcc-hardened.done"
    fi

    if [[ ! -f "${ROOT}/libc-hardened.done" ]]; then
        export CXX="false" CC="${SOURCE}/release/build.sh compiler target"
        export CFLAGS="${OPT_FLAGS} -g2 ${ARCH_FLAGS} -fno-lto"
        CFLAGS+=" -fstack-clash-protection -ftrivial-auto-var-init=zero"
        CFLAGS+=" -ffunction-sections -fdata-sections"
        export LDFLAGS="-Wl,--hash-style=both,--gc-sections"
        (
            mkdir -p -- "${ROOT}/glibc-release"
            cd -- "${ROOT}/glibc-release"
            ../glibc/configure --prefix=/usr \
                --with-headers="${SYSROOT}/usr/include" \
                --enable-kernel="$KERNEL_MIN" \
                --enable-static-pie --disable-werror \
                --enable-stack-protector=strong --enable-cet \
                --enable-fortify-source=3 --enable-bind-now
            printf "%s\n" "LDFLAGS.so += -Wl,--gc-sections" \
                "LDFLAGS.so += -Wl,--undefined=__x86_cacheinfo_p" \
                "LDFLAGS-rtld += -Wl,--gc-sections" > configparms
            make -j "${JOBS:-8}"
            make install_root="$SYSROOT" install

            touch -- "${ROOT}/libc-hardened.done"
        )
    fi
)

deps() (
    environment

    ROOT="$BUILD"
    DEPS="${BUILD}/deps"
    export CC="${SOURCE}/release/build.sh compiler target"
    export AR="${TARGET}-ar" RANLIB="${TARGET}-ranlib"
    mkdir -p -- "$DEPS"

    if [[ ! -f "${ROOT}/deps.done" ]]; then
        for NAME in "gmp" "openssl"; do
            mkdir -p -- "${ROOT}/${NAME}"
        done

        tar --no-same-owner -xf "/downloads/${ARCHIVES[gmp]}" \
            -C "${ROOT}/gmp" --strip-components=1
        tar --no-same-owner -xf "/downloads/${ARCHIVES[openssl]}" \
            -C "${ROOT}/openssl" --strip-components=1

        # Keep AVX2 assembly; omit AVX-512 implementations and dispatch.
        for FILE in chacha/asm/chacha-x86_64.pl \
                poly1305/asm/poly1305-x86_64.pl; do
            sed -i '/^open OUT/i $avx = 2 if $avx > 2;' \
                "${ROOT}/openssl/crypto/$FILE"
        done
        for FILE in "${ROOT}/openssl/crypto/bn/asm/"rsaz-?k-avx512.pl; do
            sed -i '/^open OUT/i $avx512ifma = 0;' "$FILE"
        done
        for FILE in modes/asm/aes-gcm-avx512.pl \
                aes/asm/aesni-xts-avx512.pl; do
            sed -i '/^open OUT/i $avx512vaes = 0;' \
                "${ROOT}/openssl/crypto/$FILE"
        done

        export CFLAGS="${OPT_FLAGS} -g2 -std=gnu11 ${ARCH_FLAGS}"
        CFLAGS+=" -fPIC -fno-lto"
        CFLAGS+=" -fstack-protector-strong -fstack-clash-protection"
        CFLAGS+=" -fcf-protection=full -D_FORTIFY_SOURCE=3"
        CFLAGS+=" -ftrivial-auto-var-init=zero"
        CFLAGS+=" -ffunction-sections -fdata-sections"
        export LDFLAGS="-Wl,-z,relro,-z,now,--hash-style=both,--gc-sections"

        (
            cd -- "${ROOT}/gmp"
            ./configure --prefix=/usr --host="$TARGET" --with-pic \
                --enable-shared --enable-static
            make -j "$JOBS"
            make -j "$JOBS" check
            make DESTDIR="$DEPS" install
        )

        (
            cd -- "${ROOT}/openssl"
            ./Configure linux-x86_64 --prefix=/usr --libdir=lib \
                --openssldir=/etc/ssl shared no-module no-dso no-zlib \
                no-comp no-engine no-legacy no-tests no-sock \
                no-ec no-dh no-dsa no-cms no-cmp no-ocsp no-ts no-srp no-srtp
            make -j "$JOBS" build_libs
            make DESTDIR="$DEPS" install_dev
        )
        touch -- "${ROOT}/deps.done"
    fi

    # Let the compiler find target dependencies without host search paths.
    cp -a -- "${DEPS}/usr/include/." "${SYSROOT}/usr/include/"
    cp -a -- "${DEPS}/usr/lib/." "${SYSROOT}/usr/lib/"
    export PKG_CONFIG_LIBDIR="${DEPS}/usr/lib/pkgconfig"
    export PKG_CONFIG_SYSROOT_DIR="$SYSROOT"
)

tools() (
    environment

    PREFIX="$TOOLS"
    BUILD+="/packaging-build"
    [[ -f "${PREFIX}/ready" ]] && exit 0

    export CC="${SOURCE}/release/build.sh compiler packaging"
    export CXX="${SOURCE}/release/build.sh compiler g++"
    export CFLAGS="$FLAGS"
    export CXXFLAGS="$CFLAGS"
    export LDFLAGS="$LINK_FLAGS"
    unset CPPFLAGS LDLIBS MAKEFLAGS MFLAGS
    export MAKE_OPTS="-j ${JOBS:-8}"
    export TMPDIR="${BUILD}/tmp"
    mkdir -p -- "$BUILD" "$PREFIX" "$TMPDIR"

    extract "${ARCHIVES[pyenv]}" "${BUILD}/pyenv"
    extract "${ARCHIVES[zopfli]}" "${BUILD}/zopfli"
    extract "${ARCHIVES[advancecomp]}" "${BUILD}/advancecomp"

    export PYTHON_BUILD_CACHE_PATH="/downloads"
    export PYTHON_BUILD_BUILD_PATH="${BUILD}/python"
    export PYTHON_BUILD_SKIP_MIRROR=1
    export OPT="$CFLAGS"
    unset CONFIGURE_OPTS PYTHON_CONFIGURE_OPTS
    if [[ ! -f "${PREFIX}/python.ready" ]]; then
        "${BUILD}/pyenv/plugins/python-build/bin/python-build" \
            "${VERSIONS[Python]}" "${PREFIX}/python"
        touch -- "${PREFIX}/python.ready"
    fi
    export PATH="${PREFIX}/python/bin:${PATH}"
    python3 -m pip install --upgrade pip setuptools wheel
    mkdir -p -- "${BUILD}/cython-source"
    rm -f -- "${BUILD}/cython-source/"*.tar.gz
    python3 -m pip download --no-deps --no-cache-dir --no-binary=:all: \
        --no-build-isolation --dest "${BUILD}/cython-source" \
        "$CYTHON_REQUIREMENT"
    (cd -- "${BUILD}/cython-source"; sha256sum -- *.tar.gz) \
        > "${PREFIX}/cython.sha256"
    rm -rf -- "${BUILD}/cython"
    mkdir -- "${BUILD}/cython"
    tar --no-same-owner -xf "${BUILD}/cython-source/"*.tar.gz \
        -C "${BUILD}/cython" --strip-components=1
    python3 -m pip install --verbose --force-reinstall --no-deps \
        --no-cache-dir --no-build-isolation "${BUILD}/cython"
    python3 -m pip list --format=freeze > "${PREFIX}/python.lock"

    (
        cd -- "${BUILD}/advancecomp"
        ./configure --prefix="$PREFIX"
        make -j "${JOBS:-8}"
        make install
    )
    make -C "${BUILD}/zopfli" -j "${JOBS:-8}" zopfli
    install -- "${BUILD}/zopfli/zopfli" "${PREFIX}/bin/zopfli"
    touch -- "${PREFIX}/ready"
)

package() (
    environment

    SOURCE_ROOT="$SOURCE"
    OUTPUT="/artifacts"
    VERSION="$(make --no-print-directory -s version)"
    ABI="${VERSION%%.*}"
    DEPS="${BUILD}/deps/usr"
    WORK="$(mktemp -d "${BUILD}/package.XXXXXX")"
    trap 'rm -rf -- "$WORK"' EXIT
    export CC="${BUILD}/toolchain/usr/bin/${TARGET}-gcc"
    export AR="${TARGET}-ar"
    OBJCOPY="${TARGET}-objcopy"
    ZOPFLI="${TOOLS}/bin/zopfli"
    ADVZIP="${TOOLS}/bin/advzip"

    cp -- "${SOURCE_ROOT}/passlit-${VERSION}.tar.xz" "$OUTPUT"
    tar --no-same-owner -xf "${OUTPUT}/passlit-${VERSION}.tar.xz" -C "$WORK"
    SOURCE="${WORK}/passlit-${VERSION}"
    make -C "$SOURCE" archive
    cp -- "${SOURCE}/passlit-${VERSION}.tar.xz" "$OUTPUT"
    export CFLAGS="-Os -fvect-cost-model=cheap"
    CFLAGS+=" --sysroot=${BUILD}/toolchain/sysroot"
    CFLAGS+=" -frandom-seed=passlit"
    CFLAGS+=" -ffile-prefix-map=${SOURCE}=/usr/local/src/passlit"
    export CPPFLAGS="-isystem ${DEPS}/include"
    export LDFLAGS="-L${DEPS}/lib"
    export LDLIBS="-lgmp -lcrypto -pthread"
    make -C "$SOURCE" THREADS=1 PREFIX=/usr DEBUG=1

    PACKAGE="${WORK}/package"
    RUNTIME="${WORK}/runtime"
    DEBUG="${WORK}/debug"
    make -C "$SOURCE" THREADS=1 PREFIX=/usr DEBUG=1 \
        DESTDIR="$PACKAGE" install

    cat > "${WORK}/exports.map" <<'MAP'
PASSLIT_0 {
    global:
        passlit_configure;
        passlit_generate;
        passlit_generate2;
        passlit_generatep;
        passlit_generatep2;
        passlit_scope;
        passlit_scope2;
        passlit_encode;
        passlit_encodep;
        passlit_encode2;
        passlit_encodep2;
        passlit_rank;
        passlit_rankp;
        passlit_rank2;
        passlit_rankp2;
};
PASSLIT_PRIVATE {
    global:
        passlit_length_limit;
        passlit_sampler_create;
        passlit_sampler_free;
        passlit_sampler_length;
        passlit_sampler_sample;
        passlit_sampler_batch;
        passlit_sampler_count;
        passlit_sampler_encode;
        passlit_sampler_rank;
        passlit_sampler_encode_batch;
        passlit_sampler_rank_batch;
    local: *;
};
MAP
    SHARED="libpasslit.so.${VERSION}"
    "$CC" $CFLAGS $ARCH_FLAGS -fPIC -shared -fno-lto \
        -Wl,-soname,libpasslit.so.${ABI},-z,relro,-z,now,--hash-style=both \
        "-Wl,--version-script=${WORK}/exports.map" \
        -Wl,--gc-sections -o "${PACKAGE}/usr/lib/${SHARED}" \
        "${SOURCE}/libpasslit.o" "-L${DEPS}/lib" -lgmp -lcrypto -pthread
    chmod 644 -- "${PACKAGE}/usr/lib/${SHARED}"
    ln -s -- "$SHARED" "${PACKAGE}/usr/lib/libpasslit.so.${ABI}"
    ln -s -- "libpasslit.so.${ABI}" "${PACKAGE}/usr/lib/libpasslit.so"
    "$CC" $CFLAGS $ARCH_FLAGS -fPIE -pie -fno-lto \
        -Wl,-z,relro,-z,now,--hash-style=both,--gc-sections \
        -Wl,-rpath,'${ORIGIN}/../lib' \
        -o "${PACKAGE}/usr/bin/passlit" "${SOURCE}/tool.o" \
        "-L${PACKAGE}/usr/lib" "-Wl,-rpath-link,${DEPS}/lib" \
        "-L${DEPS}/lib" -lpasslit -lgmp -pthread
    for FILE in "bin/passlit" "lib/$SHARED"; do
        DEBUG_FILE="${FILE}.dbg"
        [[ "$FILE" == "lib/$SHARED" ]] && \
            DEBUG_FILE="lib/libpasslit.so.${ABI}.dbg"
        mkdir -p -- "${DEBUG}/$(dirname -- "$FILE")"
        "$OBJCOPY" --only-keep-debug "${PACKAGE}/usr/$FILE" \
            "${DEBUG}/${DEBUG_FILE}"
        "$OBJCOPY" --strip-all "${PACKAGE}/usr/$FILE"
        "$OBJCOPY" "--add-gnu-debuglink=${DEBUG}/${DEBUG_FILE}" \
            "${PACKAGE}/usr/$FILE"
    done
    # One compilation unit, one supplementary debug file.
    mkdir -p -- "${WORK}/split" "${DEBUG}/lib"
    (
        cd -- "${WORK}/split"
        printf '#include "%s/%s.c"\n' "$SOURCE" "sampling" \
            "$SOURCE" "passlit" "$SOURCE" "platform" > libpasslit.c
        "$CC" $CFLAGS $ARCH_FLAGS $CPPFLAGS -g2 -gsplit-dwarf \
            "-fdebug-prefix-map=${WORK}/split=/usr/lib" \
            -fhardened -fPIC -fno-lto -pthread \
            -ffunction-sections -fdata-sections -std=c99 -pedantic-errors \
            -Wall -Wextra -Wconversion -Wshadow \
            -S libpasslit.c -o libpasslit.s
        sed -i "s/libpasslit[.]dwo/libpasslit.dwo.${ABI}/" libpasslit.s
        "$CC" $CFLAGS $ARCH_FLAGS -c libpasslit.s -o libpasslit.o
        "$OBJCOPY" --extract-dwo libpasslit.o "libpasslit.dwo.${ABI}"
        "$OBJCOPY" --strip-dwo libpasslit.o
        "$AR" rcs "${PACKAGE}/usr/lib/libpasslit.a.${VERSION}" libpasslit.o
        cp -- "libpasslit.dwo.${ABI}" "${DEBUG}/lib/"
    )
    LICENSES="${PACKAGE}/usr/share/licenses/passlit"
    cp -- "${SOURCE_ROOT}/release/licenses/GCC.RUNTIME" "$LICENSES"
    cp -- "${SOURCE_ROOT}/release/licenses/notices-dynamic.txt" \
        "${LICENSES}/DEPENDENCIES.txt"

    make -C "$SOURCE" clean
    make -C "$SOURCE" THREADS=1 STATIC=1 DEBUG=0 \
        LDLIBS="${DEPS}/lib/libgmp.a ${DEPS}/lib/libcrypto.a -ldl -pthread"
    mkdir -p -- "${BUILD}/dist/relink" "${BUILD}/dist/sources"
    cp -- "${SOURCE}/tool.o" "${SOURCE}/libpasslit.a.${ABI}" \
        "${BUILD}/dist/relink/"
    cp -- "${OUTPUT}/passlit-${VERSION}.tar.xz" "${BUILD}/dist/sources/"
    cp -- /downloads/*.tar.* "${BUILD}/dist/sources/"
    cp -- "${DEPS}/lib/libgmp.a" "${DEPS}/lib/libcrypto.a" \
        "${BUILD}/dist/relink/"
    cp -- /packages.lock "${BUILD}/dist/"
    cp -- "${SOURCE_ROOT}/release/README.md" "${BUILD}/dist/BUILD.md"
    mkdir -p -- "${RUNTIME}/man" "${RUNTIME}/licenses"
    cp -- "${SOURCE}/passlit" "$RUNTIME"
    "$OBJCOPY" --strip-all "${RUNTIME}/passlit"
    cp -- "${SOURCE}/man/passlit.1" "${RUNTIME}/man/"
    cp -- "${SOURCE}/LICENSE.md" "${SOURCE}/COPYING"*.md \
        "${SOURCE_ROOT}/release/licenses/GCC.RUNTIME" "${RUNTIME}/licenses/"
    sed -e "s/@GMP_VERSION@/${VERSIONS[gmp]}/g" \
        -e "s/@OPENSSL_VERSION@/${VERSIONS[openssl]}/g" \
        -e "s/@GLIBC_VERSION@/${VERSIONS[glibc]}/g" \
        "${SOURCE_ROOT}/release/licenses/notices-static.txt" \
        > "${RUNTIME}/licenses/DEPENDENCIES.txt"
    cp -- "${BUILD}/gmp/COPYING.LESSERv3" "${RUNTIME}/licenses/GMP.txt"
    cp -- "${BUILD}/openssl/LICENSE.txt" "${RUNTIME}/licenses/OpenSSL.txt"
    cp -- "${BUILD}/toolchain/glibc/COPYING.LIB" \
        "${RUNTIME}/licenses/glibc.txt"
    cp -- "${BUILD}/toolchain/glibc/LICENSES" \
        "${RUNTIME}/licenses/glibc-NOTICES.txt"

    LD_LIBRARY_PATH="${DEPS}/lib" "${PACKAGE}/usr/bin/passlit" \
        --seed "package-check" -n 10 > "${WORK}/dynamic.samples"
    "${RUNTIME}/passlit" --seed "package-check" -n 10 \
        > "${WORK}/static.samples"
    cmp -- "${WORK}/dynamic.samples" "${WORK}/static.samples"
    (
        cd -- "${SOURCE}/test"
        LD_LIBRARY_PATH="${DEPS}/lib" \
            PASSLIT="${PACKAGE}/usr/bin/passlit" sh tool.sh
        PASSLIT="${RUNTIME}/passlit" sh tool.sh
    )
    readelf -d "${PACKAGE}/usr/bin/passlit" | grep -q "libpasslit.so.${ABI}"
    if readelf -lWd "${RUNTIME}/passlit" | grep -Eq "INTERP|NEEDED"; then
        printf "%s\n" "Runtime is not static" >&2
        exit 1
    fi

    for KIND in "package" "runtime" "debug"; do
        TREE="${WORK}/${KIND}"
        [[ "$KIND" == "package" ]] && TREE+="/usr"
        tar -C "$TREE" --sort=name --owner=0 --group=0 --numeric-owner \
            --mtime="@${SOURCE_DATE_EPOCH}" \
            --use-compress-program="xz --threads=1 -9e" \
            -cf "${OUTPUT}/passlit-${VERSION}-${KIND}-x86_64.tar.xz" .
    done

    unset CC AR CFLAGS CPPFLAGS LDFLAGS LDLIBS
    (cd -- "${SOURCE}/python"; python3 setup.py sdist --dist-dir "$WORK")
    mkdir -- "${WORK}/sdist"
    tar --no-same-owner -xf "${WORK}/passlit-${VERSION}.tar.gz" \
        -C "${WORK}/sdist"
    tar -C "${WORK}/sdist" --sort=name --owner=0 --group=0 --numeric-owner \
        --mtime="@$SOURCE_DATE_EPOCH" \
        -cf "${WORK}/sdist.tar" "passlit-$VERSION"
    "$ZOPFLI" --gzip --i50 -c "${WORK}/sdist.tar" \
        > "${OUTPUT}/passlit-${VERSION}.tar.gz"
    export CC="${BUILD}/toolchain/usr/bin/${TARGET}-gcc"
    export PASSLIT_DEPS="$DEPS" PASSLIT_NOTICES="${RUNTIME}/licenses"
    export CFLAGS="${OPT_FLAGS} -Wl,-s -frandom-seed=passlit-python"
    CFLAGS+=" --sysroot=${BUILD}/toolchain/sysroot"
    CFLAGS+=" -ffile-prefix-map=${SOURCE}=/usr/local/src/passlit"
    python3 -m pip wheel --no-deps --no-build-isolation \
        --wheel-dir "${WORK}/wheels" "${SOURCE}/python"
    cp -- "${WORK}/wheels/passlit-${VERSION}-"*.whl "$OUTPUT"
    "$ADVZIP" --recompress --shrink-insane --iter=50 --keep-file-time \
        -- "${OUTPUT}/passlit-${VERSION}-"*.whl
    (cd -- "$OUTPUT"; sha256sum -- passlit-* > SHA256SUMS)
    printf "%s\n" "Release artifacts: ${OUTPUT}"
)

distclean() {
    local ROOT BUILD OUTPUT ID MOUNT IMAGE IDS FORMAT
    local IMAGES=("localhost/passlit-builder:latest")

    ROOT="$(pwd -P)"
    BUILD="$(realpath -m -- "${BUILD_DIR:-out/release}")"
    OUTPUT="$(realpath -m -- "${OUTPUT_DIR:-out/artifacts}")"
    for MOUNT in "$BUILD" "$OUTPUT"; do
        if [[ "${ROOT}/" == "${MOUNT}/"* ]]; then
            printf "%s\n" "Refusing to remove source directory: ${MOUNT}" >&2
            return 64
        fi
    done

    if [[ -s out/builder.image ]]; then
        IMAGES+=("$(cat -- out/builder.image)")
    fi
    FORMAT='{{range .Mounts}}'
    FORMAT+='{{if eq .Destination "/usr/local/src/passlit"}}'
    FORMAT+='{{.Source}}{{end}}{{end}}'
    IDS="$(podman ps --all --quiet)"
    for ID in $IDS; do
        MOUNT="$(podman inspect --format "$FORMAT" "$ID")"
        if [[ "$MOUNT" == "$ROOT" ]]; then
            podman stop --time 2 "$ID"
            podman rm --ignore "$ID"
        fi
    done
    for IMAGE in "${IMAGES[@]}"; do
        if podman image exists "$IMAGE"; then
            podman rmi "$IMAGE"
        fi
    done
    make distclean
    rm -rf -- "$BUILD" "$OUTPUT" "${ROOT}/out"
}

host() {
    # shellcheck source=release/sources.sh
    source "$(dirname -- "${BASH_SOURCE[0]}")/sources.sh"
    MODE="${1:-all}"
    [[ "$MODE" == "all" || "$MODE" == "package" ]] || exit 64
    ROOT="$(pwd)"
    BUILD="$(realpath -m -- "${BUILD_DIR:-out/release}")"
    OUTPUT="$(realpath -m -- "${OUTPUT_DIR:-out/artifacts}")"
    TOOLS="${BUILD}/packaging"
    STATE="$(mktemp -d)"

    cleanup() {
        trap "" INT TERM
        if [[ -s "${STATE}/cid" ]]; then
            podman stop --time 2 "$(cat -- "${STATE}/cid")" \
                >/dev/null 2>&1 || true
        fi
        rm -rf -- "$STATE"
    }

    trap cleanup EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM
    mkdir -p -- "$BUILD" "$OUTPUT" "$TOOLS" out/downloads

    if [[ "$MODE" == "all" ]]; then
        for NAME in "${!ARCHIVES[@]}"; do
            FILE="${ARCHIVES[$NAME]}"
            HASH="${HASHES[$NAME]}"
            URL="${URLS[$NAME]}"
            if [[ ! -f "out/downloads/${FILE}" ]]; then
                curl --fail --location --retry 3 --retry-all-errors \
                    --output "out/downloads/${FILE}.tmp" -- "$URL"
                mv -- "out/downloads/${FILE}.tmp" "out/downloads/${FILE}"
            fi
            printf "%s  %s\n" "$HASH" "out/downloads/${FILE}" | sha256sum -c -
        done
    fi

    if [[ ! -f out/builder.image ]]; then
        podman build --force-rm -t passlit-builder release
        podman image inspect --format "{{.Id}}" passlit-builder \
            > out/builder.image
    fi
    IMAGE="$(cat out/builder.image)"

    run() {
        rm -f -- "${STATE}/cid"
        podman run --rm --init --sig-proxy=false --cidfile "${STATE}/cid" \
            --userns=keep-id -e "JOBS=${JOBS:-8}" \
            -v "${ROOT}:/usr/local/src/passlit:ro" \
            -v "${BUILD}:/var/tmp/build/passlit" \
            -v "${ROOT}/out/downloads:/downloads:ro" \
            -v "${TOOLS}:/opt/passlit-packaging" \
            -v "${OUTPUT}:/artifacts" "$@" &
        wait "$!"
    }

    printf "%s\n" "Container build: ${BUILD}; packaging tools: ${TOOLS}" >&2
    if [[ "$MODE" == "all" ]]; then
        run --network=none "$IMAGE" -c \
            'bash release/build.sh --toolchain && bash release/build.sh --deps'
        run "$IMAGE" release/build.sh --tools
    fi
    make archive
    printf "%s\n" "Container artifacts: ${OUTPUT}" >&2
    run --network=none "$IMAGE" release/build.sh --package
}

case "${1:-all}" in
    distclean) distclean ;;
    rebuild) distclean; host all ;;
    compiler) shift; compiler "$@" ;;
    --toolchain) toolchain ;;
    --deps) deps ;;
    --tools) tools ;;
    --package) package ;;
    *) host "$@" ;;
esac
