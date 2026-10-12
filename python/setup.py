"""Build the extension and source package."""

import os
import shutil
import subprocess
import sys
from pathlib import Path

from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext
from setuptools.command.egg_info import egg_info
from setuptools.command.sdist import sdist


NATIVE = Path("src") if Path("src/passlit.h").exists() else Path("..")
VERSION = int(next(
    line.split()[2].removesuffix("U")
    for line in (NATIVE / "passlit.h").read_text().splitlines()
    if line.startswith("#define PASSLIT_VERSION ")
), 16)


class SourceArchive(sdist):
    """Keep native sources inside the standalone archive."""

    def make_release_tree(self, base_dir: str, files: list[str]) -> None:
        """Stage native inputs only in the archive tree."""
        super().make_release_tree(base_dir, files)
        target = Path(base_dir) / "src"
        target.mkdir(exist_ok=True)
        for name in [
            "passlit.c",
            "sampling.c",
            "platform.c",
            "passlit.h",
            "sampling.h",
            "platform.h",
            "config.mk",
        ]:
            if not (target / name).exists():
                shutil.copy2(NATIVE / name, target / name)


class NativeBuild(build_ext):
    """Use the project Makefile."""

    def build_extension(self, ext: Extension) -> None:
        """Compile with the project flags."""
        output = Path(self.get_ext_fullpath(ext.name)).resolve()
        subprocess.run(
            [
                "make",
                f"PYTHON={sys.executable}",
                f"OUT={output}",
                f"SOURCE_DIR={NATIVE.resolve()}",
                "-B",
            ],
            check=True,
        )


class LicenseMetadata(egg_info):
    """Include bundled dependency licenses."""

    def run(self) -> None:
        """Stage notices for wheel metadata."""
        if os.environ.get("PASSLIT_DEPS"):
            notices = Path(os.environ["PASSLIT_NOTICES"])
            licenses = Path("deps")
            licenses.mkdir(parents=True, exist_ok=True)
            for name in ["GMP.txt", "OpenSSL.txt", "GCC.RUNTIME"]:
                shutil.copy2(notices / name, licenses / name)
            (licenses / "DEPENDENCIES.txt").write_text(
                "GMP: LGPLv3 or later; see GMP.txt.\n"
                "OpenSSL libcrypto: Apache-2.0; see OpenSSL.txt.\n"
                "GCC runtime: GPLv3 with exception; see GCC.RUNTIME.\n"
                "glibc is provided by the host.\n"
            )
            # Setuptools extends distutils metadata at runtime.
            files: list[str] = getattr(
                self.distribution.metadata, "license_files"
            )
            for path in sorted(licenses.iterdir()):
                if str(path) not in files:
                    files.append(str(path))
        super().run()


setup(
    version=".".join(str((VERSION >> shift) & 255) for shift in (24, 16, 8)),
    packages=[],
    ext_modules=[
        Extension(
            "passlit",
            sources=["passlit.pyx"],
            py_limited_api=True,
        )
    ],
    cmdclass={
        "build_ext": NativeBuild,
        "egg_info": LicenseMetadata,
        "sdist": SourceArchive,
    },
    options={
        "bdist_wheel": {"py_limited_api": "cp313"},
    },
)
