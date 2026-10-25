import tempfile
import unittest
from pathlib import Path
from rsx_codegen_experiment import MARKER, CONTROL, patch_source, manifest


class CodegenEvidence(unittest.TestCase):
    def test_narrow_patch(self):
        original = '// original\n' + MARKER + '\t\t{ reserve(42); }\n'
        updated = patch_source(original)
        self.assertEqual(updated.replace(CONTROL, '', 1), original)
        self.assertEqual(updated.count('void grow_for_append()'), 1)

    def test_missing_marker(self):
        with self.assertRaises(ValueError):
            patch_source('void another_function() {}')

    def test_multiple_markers(self):
        with self.assertRaises(ValueError):
            patch_source(MARKER * 2)

    def test_reject_reapplication(self):
        with self.assertRaises(ValueError):
            patch_source(patch_source(MARKER))

    def populate(self, folder):
        for variant in ('baseline', 'candidate'):
            for ext in ('exe', 'obj', 'asm', 'build.txt'):
                (folder / f'{variant}.{ext}').write_bytes(b'evidence')

    def test_complete_evidence(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            self.populate(path)
            self.assertEqual(len(manifest(path, ['baseline', 'candidate'])), 8)

    def test_one_missing_assembly_is_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            self.populate(path)
            (path / 'candidate.asm').unlink()
            with self.assertRaises(FileNotFoundError):
                manifest(path, ['baseline', 'candidate'])

    def test_empty_assembly_is_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            self.populate(path)
            (path / 'candidate.asm').write_bytes(b'')
            with self.assertRaises(ValueError):
                manifest(path, ['baseline', 'candidate'])


if __name__ == '__main__':
    unittest.main()
