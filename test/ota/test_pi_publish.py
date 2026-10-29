"""Verify Pi publishing never exposes a manifest referencing invalid images."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

release = load('ota_release_pi_test', ROOT/'tools/ota_release.py')
server = load('ota_pi_publish', ROOT/'tools/ota_pi/publish.py')

class PiPublishTests(unittest.TestCase):
    def test_reject_bad_release_preserves_current(self):
        with tempfile.TemporaryDirectory() as temp:
            p = Path(temp)
            for slot, base in release.SLOTS.items():
                (p/slot).write_bytes(struct.pack('<II', 0x20020000, base+9)+b'abcd')
            (p/'boot').write_bytes(b'bootboot')
            incoming, public = p/'incoming', p/'public'
            release.publish(1, p/'A', p/'B', p/'boot', incoming)
            server.publish(incoming, public)
            original = (public/'manifest.json').read_bytes()
            server.publish(incoming, public)  # same publication is idempotent
            self.assertEqual((public/'manifest.json').read_bytes(), original)
            m = release.publish(2, p/'A', p/'B', p/'boot', incoming)
            image = incoming/m['banks']['B']['file']
            image.write_bytes(image.read_bytes()[:-1]+b'x')
            with self.assertRaisesRegex(ValueError, 'SHA256'):
                server.publish(incoming, public)
            self.assertEqual((public/'manifest.json').read_bytes(), original)
            release.publish(2, p/'A', p/'B', p/'boot', incoming)
            server.publish(incoming, public)
            current = (public/'manifest.json').read_bytes()
            self.assertEqual(json.loads(current)['version'], 2)
            release.publish(1, p/'A', p/'B', p/'boot', incoming)
            with self.assertRaisesRegex(ValueError, 'version'):
                server.publish(incoming, public)
            self.assertEqual((public/'manifest.json').read_bytes(), current)

if __name__ == '__main__':
    unittest.main()
