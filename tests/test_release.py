import unittest

from tools.release import components, read_version, should_publish


class ReleaseVersionTests(unittest.TestCase):
    def test_current_app_version_fits_windows_file_version(self):
        self.assertEqual(len(components(read_version())), 3)
        for version in ("v1.2.3", "1.02.3", "1.2", "1.2.3-preview", "65536.0.0"):
            with self.subTest(version=version), self.assertRaises(ValueError):
                components(version)

    def test_only_a_new_version_can_publish(self):
        self.assertTrue(should_publish("0.4.10", ["v0.4.9", "dev"]))
        self.assertFalse(should_publish("0.4.10", ["v0.4.9", "v0.4.10"]))
        with self.assertRaises(ValueError):
            should_publish("0.4.2", ["v0.4.10"])
