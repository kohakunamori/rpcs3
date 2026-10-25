import copy
import csv
import io
import unittest

import run_rsx_array_validation as bench


def fixture(policy="baseline"):
    rows = []
    for t, mode, n in sorted(bench.CASES):
        repeats = max(16, min(32768, 262144 // (n + 1)))
        rows.append(dict(policy=policy, type=t, mode=mode, count=n, repeats=repeats,
                         elapsed_ms=10, checksum=n * repeats * (n + repeats) // 2,
                         capacity=n, reserved_bytes=n * (8 if t == "u64" else 64), growths=0))
    return rows


def encode(rows):
    text = io.StringIO()
    writer = csv.DictWriter(text, fieldnames=bench.FIELDS)
    writer.writeheader()
    writer.writerows(rows)
    return text.getvalue()


class ResultsValidation(unittest.TestCase):
    def test_complete(self):
        self.assertEqual(len(bench.parse_samples(encode(fixture()), "baseline")), 78)

    def test_missing_case(self):
        with self.assertRaises(ValueError):
            bench.parse_samples(encode(fixture()[:-1]), "baseline")

    def test_duplicate(self):
        rows = fixture()
        rows.append(rows[0])
        with self.assertRaises(ValueError):
            bench.parse_samples(encode(rows), "baseline")

    def test_bad_duration(self):
        for number in (float("nan"), float("inf"), -1, 0):
            rows = fixture()
            rows[0]["elapsed_ms"] = number
            with self.assertRaises(ValueError):
                bench.parse_samples(encode(rows), "baseline")

    def test_checksum(self):
        rows = fixture()
        rows[0]["checksum"] += 1
        with self.assertRaises(ValueError):
            bench.parse_samples(encode(rows), "baseline")

    def test_policy(self):
        with self.assertRaises(ValueError):
            bench.parse_samples(encode(fixture()), "grow25")

    def test_size_and_repeats(self):
        for field, val in (("repeats", 0), ("reserved_bytes", 3), ("count", -1)):
            rows = fixture()
            rows[0][field] = val
            with self.assertRaises(ValueError):
                bench.parse_samples(encode(rows), "baseline")

    def test_retained_growth(self):
        rows = fixture()
        for row in rows:
            if row["mode"] == "reuse":
                row["growths"] = 1
                break
        with self.assertRaises(ValueError):
            bench.parse_samples(encode(rows), "baseline")

    def measured(self):
        rows = []
        for p in range(4):
            for v in ("baseline", "candidate"):
                samples = fixture("baseline" if v == "baseline" else "grow25")
                for sample in samples:
                    sample["elapsed_ms"] = 10 if v == "baseline" else 8
                    rows.append(dict(pair=p, variant=v, **sample))
        return rows

    def test_summary(self):
        result = bench.summarize(self.measured(), 4)
        self.assertEqual(len(result), 78)
        self.assertAlmostEqual(result[0]["paired_median_change_percent"], -20)
        self.assertEqual(result[0]["faster_pairs"], 4)

    def test_missing_pair(self):
        with self.assertRaises(ValueError):
            bench.summarize(self.measured()[:-1], 4)

    def test_mismatched_work(self):
        rows = self.measured()
        rows[0]["repeats"] += 1
        with self.assertRaises(ValueError):
            bench.summarize(rows, 4)

    def test_duplicate_pair(self):
        rows = self.measured()
        rows.append(copy.copy(rows[0]))
        with self.assertRaises(ValueError):
            bench.summarize(rows, 4)


if __name__ == "__main__":
    unittest.main()
