"""Reference scanner regressions, using real DEX definitions without executing guest code."""
import importlib.util
from pathlib import Path
from types import SimpleNamespace
import unittest

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("check_dex", ROOT / "scripts/check-dex.py")
scanner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(scanner)


class DexCheck(unittest.TestCase):
    def test_bundled_tests_have_no_missing_executable_references(self):
        boot = [scanner.Dex((ROOT / p).read_bytes()) for p in ("vm/core/henge-core.dex", "vm/android/henge-android.dex")]
        app = scanner.Dex((ROOT / "vm/tests/vmtest.dex").read_bytes())
        self.assertEqual(scanner.check([app], boot), [])

    def test_missing_and_inherited_members(self):
        definitions = {"LA;": dict(parents=[], methods={("ok", "()I")}, fields={("value", "I")}),
                       "LB;": dict(parents=["LA;"], methods=set(), fields=set())}
        app = SimpleNamespace(classes=definitions, types=["LB;", "LMissing;", "[I"],
                              methods=[("LB;", "ok", "()I"), ("LB;", "missing", "()V"), ("LB;", "<init>", "()V")],
                              fields=[("LB;", "value", "I"), ("LB;", "absent", "J")])
        missing = scanner.check([app], [])
        self.assertEqual(len(missing), 4)
        self.assertIn(dict(kind="class", name="LMissing;"), missing)
        self.assertIn(dict(kind="field", name="LB;->absentJ"), missing)

    def test_malformed_dex(self):
        for raw in (b"", b"dex\n" + bytes(108), (ROOT / "vm/tests/vmtest.dex").read_bytes()[:120]):
            with self.assertRaises((ValueError, IndexError)):
                scanner.Dex(raw)


if __name__ == "__main__":
    unittest.main()
