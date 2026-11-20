"""Creation workflow self-tests own their OUT parent in a fresh checkout."""
import shutil
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

REPOSITORY = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(REPOSITORY / 'tools' / 'tests'), str(REPOSITORY / 'tools')]
import coop_editor_creation_save as creation
from coop_editor_creation_save_contracts import workflow_contracts


class WorkflowOutputTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='creation-output-tests-')
        self.root = Path(self.directory.name).resolve()
        self.assertEqual(self.root.parent, Path(tempfile.gettempdir()).resolve())
        self.assertTrue(self.root.name.startswith('creation-output-tests-'))
        self.addCleanup(self.directory.cleanup)
        self.output = self.root / 'out'
        # The existing workflow exercises its real input recipes with fake
        # games, native readers and process owners. No game is connected.
        shutil.copytree(REPOSITORY / 'tools' / 'tests' / 'fixtures' / 'editor_creation',
                        self.root / 'tools' / 'tests' / 'fixtures' / 'editor_creation')

    def run_workflows(self):
        with patch.object(creation, 'ROOT', self.root):
            checks = workflow_contracts(creation)
        self.assertGreater(checks, 0)

    def test_fresh_checkout_creates_output_parent_and_cleans_only_its_results(self):
        self.assertFalse(self.output.exists())
        self.run_workflows()
        self.assertTrue(self.output.is_dir())
        self.assertEqual(list(self.output.iterdir()), [])

    def test_existing_output_keeps_unrelated_bytes(self):
        self.output.mkdir()
        marker = self.output / 'unrelated-existing-artifact.bin'
        original = b'preserve existing output\x00\xff'
        marker.write_bytes(original)
        self.run_workflows()
        self.assertEqual(marker.read_bytes(), original)
        self.assertEqual(list(self.output.iterdir()), [marker])

    def test_file_at_output_path_is_rejected_without_replacing_it(self):
        original = b'out is an existing file'
        self.output.write_bytes(original)
        with self.assertRaises(OSError):
            self.run_workflows()
        self.assertEqual(self.output.read_bytes(), original)


if __name__ == '__main__':
    unittest.main()
