# Release

Configure versions and hashes in `sources.sh`, flags in `build.sh`, and
Debian packages in `Containerfile`.
Version changes need a fresh build directory.

Needs Podman, GNU Make and curl. From the project root:

```sh
bash release/build.sh
bash release/build.sh distclean  # Remove outputs and builder images.
bash release/build.sh rebuild  # Clean everything, then build.
bash release/build.sh package  # Same; reuse completed build stages.
```

Missing prerequisites are built automatically.

Six artifacts and `SHA256SUMS`: `out/artifacts/`.
Toolkit, downloads and Podman storage also stay under `out/`.
