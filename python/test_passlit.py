"""Check the Python binding."""

import unittest
from concurrent.futures import ThreadPoolExecutor

import passlit


class Sampling(unittest.TestCase):
    """Exercise conversion, batching and native errors."""

    def test_default_settings(self) -> None:
        """Default list, length and alphabet."""
        samples = passlit.generate()
        self.assertEqual(len(samples), 1)
        sample = samples[0]
        self.assertEqual(len(sample), 22)
        self.assertTrue(sample.isascii())

    def test_vector(self) -> None:
        """Fixed seed vector."""
        self.assertEqual(
            passlit.generate(
                "c*",
                "abcd",
                passlit.PASSLIT_LENGTH,
                8,
                "example",
                0,
                0,
            ),
            ["dcadcadc"],
        )

    def test_threads(self) -> None:
        """Concurrent calls preserve seeded results."""

        def sample(index: int) -> list[str]:
            return list(
                passlit.generate(
                    None, None, passlit.PASSLIT_LENGTH, 40, "threads", index, 1
                )
            )

        expected = list(map(sample, range(16)))
        with ThreadPoolExecutor(4) as pool:
            self.assertEqual(list(pool.map(sample, range(16))), expected)

    def test_batches(self) -> None:
        """Batch order, automatic workers and empty output."""
        expected = [
            passlit.generate(seed="batch", sample_index=i)[0]
            for i in range(11, 311)
        ]
        for workers in [0, 1]:
            self.assertEqual(
                passlit.generate(
                    seed="batch", sample_index=11, count=300, workers=workers
                ),
                expected,
            )
        self.assertEqual(passlit.generate(count=0), [])
        for kwargs in [{"count": -1}, {"workers": -1}]:
            with self.assertRaises(OverflowError):
                passlit.generate(**kwargs)

    def test_errors(self) -> None:
        """Invalid settings become exceptions."""
        for groups, custom, seed in [("", None, None), ("c", "", None)]:
            with self.subTest(groups=groups), self.assertRaises(ValueError):
                passlit.generate(
                    groups, custom, passlit.PASSLIT_LENGTH, 8, seed, 0, 1
                )
        for size_val, index in [(-1, 0), (8, -1), (8, 2**64)]:
            with self.assertRaises(OverflowError):
                passlit.generate(
                    None,
                    None,
                    passlit.PASSLIT_LENGTH,
                    size_val,
                    None,
                    index,
                    1,
                )
        with self.assertRaises(ValueError):
            passlit.generate(None, None, 99, 8, None, 0, 1)

    def test_c_strings(self) -> None:
        """Empty strings, None and embedded zeros stay distinct."""

        def sample(
            groups: str | None, custom: str | None, seed: str | None
        ) -> list[str]:
            return list(
                passlit.generate(
                    groups, custom, passlit.PASSLIT_LENGTH, 8, seed, 0, 0
                )
            )

        self.assertEqual(
            sample("c*", "abcd", ""), sample("c*\0U", "abcd\0xyz", "\0ignored")
        )
        self.assertEqual(
            sample("U", None, "x"), sample("U", None, "x\0ignored")
        )
        self.assertEqual(len(sample(None, None, "")[0]), 8)
        with self.assertRaises(ValueError):
            sample("", None, "")
        with self.assertRaises(ValueError):
            sample("U", "", "")

    def test_arguments(self) -> None:
        """Defaults, positional arguments and keywords agree."""
        self.assertEqual(
            passlit.generate(seed="defaults"),
            passlit.generate(
                None,
                None,
                passlit.PASSLIT_BITS_DOWN,
                128,
                "defaults",
                0,
                1,
            ),
        )
        expected = passlit.generate(
            "c*", "abcd", passlit.PASSLIT_LENGTH, 8, "example", 0, 0
        )
        self.assertEqual(
            passlit.generate(
                patterns=0,
                sample_index=0,
                seed="example",
                size_val=8,
                size_kind=passlit.PASSLIT_LENGTH,
                custom="abcd",
                groups="c*",
            ),
            expected,
        )
        self.assertEqual(
            passlit.generate(
                "c*",
                "abcd",
                passlit.PASSLIT_LENGTH,
                8,
                "example",
                0,
                patterns=0,
            ),
            expected,
        )

    def test_pattern_flag(self) -> None:
        """Any nonzero pattern flag enables the same rules."""
        expected = passlit.generate(seed="flags", patterns=1)
        for flag in (2, -1):
            self.assertEqual(
                passlit.generate(seed="flags", patterns=flag), expected
            )

    def test_bounds(self) -> None:
        """Last valid index and excessive length."""
        self.assertEqual(
            passlit.generate(
                "c", "x", passlit.PASSLIT_LENGTH, 1, None, 2**64 - 1, 0
            ),
            ["x"],
        )
        with self.assertRaises(ValueError):
            passlit.generate(
                None, None, passlit.PASSLIT_LENGTH, 65537, None, 0, 0
            )


class Ordinals(unittest.TestCase):
    """Scope counts and batch conversions."""

    def test_scope(self) -> None:
        """Exact counts and both entropy bounds."""
        for pool, length, total, bits in [
            ("a", 1, 1, (0, 0)),
            ("abc", 1, 3, (1, 2)),
            ("ab", 8, 256, (8, 8)),
            ("ab", 160, 2**160, (160, 160)),
        ]:
            self.assertEqual(
                passlit.test("c", pool, passlit.PASSLIT_LENGTH, length, 0),
                dict(length=length, scope=total, entropy_bits=bits),
            )

    def test_exhaustive(self) -> None:
        """Every ordinal in a small scope round-trips."""
        ranks = list(range(256))
        samples = passlit.encode(
            ranks, "c", "ab", passlit.PASSLIT_LENGTH, 8, 0
        )
        self.assertEqual(samples[0], "aaaaaaaa")
        self.assertEqual(samples[-1], "bbbbbbbb")
        self.assertEqual(
            passlit.rank(samples, "c", "ab", passlit.PASSLIT_LENGTH, 8, 0),
            ranks,
        )

    def test_integer_bytes(self) -> None:
        """Zero, byte boundaries and large ranks survive binary conversion."""
        settings = dict(
            groups="c", custom="ab", size_kind=passlit.PASSLIT_LENGTH,
            size_val=256, patterns=0,
        )
        ranks = [0, 1, 127, 128, 255, 256, 65535, 65536, 2**255, 2**256 - 1]
        self.assertEqual(
            passlit.rank(passlit.encode(ranks, **settings), **settings), ranks
        )

    def test_pattern_batches(self) -> None:
        """Generated batches keep ordinals across worker settings."""
        samples = passlit.generate(seed="ordinals", count=300)
        ranks = passlit.rank(samples, workers=1)
        self.assertTrue(any(value > 2**64 for value in ranks))
        self.assertEqual(passlit.rank(samples), ranks)
        self.assertEqual(passlit.encode(ranks, workers=1), samples)
        self.assertEqual(passlit.encode(iter(ranks)), samples)
        scope = passlit.test()["scope"]
        self.assertEqual(
            passlit.rank(passlit.encode([scope - 1])), [scope - 1]
        )

    def test_settings_dict(self) -> None:
        """One kwargs dict configures all four operations."""
        settings = dict(
            groups="c", custom="ab", size_kind=passlit.PASSLIT_LENGTH,
            size_val=8, patterns=0,
        )
        self.assertEqual(passlit.test(**settings)["scope"], 256)
        samples = passlit.generate(seed="kwargs", count=3, **settings)
        ranks = passlit.rank(samples, **settings)
        self.assertEqual(passlit.encode(ranks, **settings), samples)

    def test_invalid_samples(self) -> None:
        """Bad samples retain their slots as -1."""
        self.assertEqual(
            passlit.rank(
                ["aa", "bad", "", "bb"],
                "c", "ab", passlit.PASSLIT_LENGTH, 2, 0,
            ),
            [0, -1, -1, 3],
        )

    def test_errors(self) -> None:
        """Invalid configs and ranks raise, including in empty batches."""
        for values in [[-1], [passlit.test()["scope"]]]:
            with self.assertRaises(ValueError):
                passlit.encode(values)
        with self.assertRaises(TypeError):
            passlit.encode([0, 1.5])
        for samples in (None, [None], ["a", None], ["a", 123]):
            with self.subTest(samples=samples), self.assertRaises(TypeError):
                passlit.rank(samples)
        with self.assertRaises(ValueError):
            passlit.test(groups="")
        for operation in (passlit.encode, passlit.rank):
            self.assertEqual(operation([]), [])
            with self.assertRaises(ValueError):
                operation([], groups="")
            with self.assertRaises(ValueError):
                operation(
                    [], groups="U*D*", size_kind=passlit.PASSLIT_LENGTH,
                    size_val=1,
                )


if __name__ == "__main__":
    unittest.main()
