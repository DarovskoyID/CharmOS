#!/usr/bin/env python3
import os
from pathlib import Path
import tempfile
import time
import unittest
from graph_cache import snapshot


class GraphReceiptTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        for name, text in {
            'build.ninja': 'subninja child.ninja\n',
            'child.ninja': 'build example: phony\n',
            'build.ninja.d': 'build.ninja.stamp: settings.gni args.gn\n',
            'build.ninja.stamp': '', 'args.gn': 'enable_rust = false\n',
            'settings.gni': 'example = false\n', 'charmos-omitted-rust.json': '[]',
            'example.cc': 'int value = 1;\n',
        }.items():
            (self.root / name).write_text(text)

    def test_cpp_edits_keep_the_verified_graph(self):
        before = snapshot(self.root)
        (self.root / 'example.cc').write_text('int value = 2;\n')
        self.assertEqual(before, snapshot(self.root, before['manifests']))

    def test_gn_edits_with_restored_mtime_invalidate(self):
        before = snapshot(self.root)
        path = self.root / 'settings.gni'
        state = path.stat()
        time.sleep(0.002)
        path.write_text('example = true;\n')
        os.utime(path, ns=(state.st_atime_ns, state.st_mtime_ns))
        self.assertNotEqual(before, snapshot(self.root, before['manifests']))

    def test_changed_child_manifest_invalidates(self):
        before = snapshot(self.root)
        (self.root / 'child.ninja').write_text('subninja new.ninja\n')
        (self.root / 'new.ninja').write_text('build changed: phony\n')
        self.assertNotEqual(before, snapshot(self.root, before['manifests']))
        self.assertIn('new.ninja', snapshot(self.root)['manifests'])

    def test_missing_or_unknown_manifest_cannot_be_cached(self):
        (self.root / 'build.ninja').write_text('subninja $unknown/file.ninja\n')
        with self.assertRaises(ValueError):
            snapshot(self.root)

    def test_directory_membership_is_an_input(self):
        (self.root / 'templates').mkdir()
        with (self.root / 'build.ninja.d').open('a') as output:
            output.write(' templates\n')
        before = snapshot(self.root)
        (self.root / 'templates/new.yaml').write_text('example: true\n')
        self.assertNotEqual(before, snapshot(self.root, before['manifests']))


if __name__ == '__main__':
    unittest.main()
