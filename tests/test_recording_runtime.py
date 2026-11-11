"""Build-time upstream outages must not bypass source integrity checks."""
from hashlib import sha256
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
from zipfile import ZipFile

from tools import build_recording_runtime as runtime


class RecordingRuntimeTests(unittest.TestCase):
    def fixture(self, folder, content=b"exact upstream bytes"):
        root = Path(folder)
        source = b"exact upstream bytes"
        asset = ("fixture.tar.xz", "https://source.invalid/", sha256(source).hexdigest())
        bundle = root / "backup.zip"
        with ZipFile(bundle, "w") as archive:
            archive.writestr("build/recording-runtime/downloads/" + asset[0], content)
        return root, asset, bundle

    def test_network_outage_recovers_exact_source_and_then_uses_cache(self):
        with tempfile.TemporaryDirectory() as folder:
            root, asset, bundle = self.fixture(folder)
            with (patch.object(runtime, "DOWNLOADS", root), patch.object(runtime, "SOURCES", (asset,)),
                  patch.object(runtime, "source_bundle", return_value=bundle),
                  patch.object(runtime, "fetch_file", side_effect=subprocess.CalledProcessError(28, "curl")) as fetch):
                result = runtime.download(asset)
                self.assertEqual(result.read_bytes(), b"exact upstream bytes")
                self.assertEqual(runtime.download(asset), result)
                self.assertEqual(fetch.call_count, 1)

    def test_recovered_source_with_wrong_inner_hash_is_never_cached(self):
        with tempfile.TemporaryDirectory() as folder:
            root, asset, bundle = self.fixture(folder, b"wrong source")
            with (patch.object(runtime, "DOWNLOADS", root), patch.object(runtime, "SOURCES", (asset,)),
                  patch.object(runtime, "source_bundle", return_value=bundle),
                  patch.object(runtime, "fetch_file", side_effect=subprocess.CalledProcessError(28, "curl"))):
                with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                    runtime.download(asset)
                self.assertFalse((root / asset[0]).exists())

    def test_published_bundle_must_match_pinned_outer_hash(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)

            def corrupt_download(_url, target):
                target.write_bytes(b"untrusted archive")

            with (patch.object(runtime, "DOWNLOADS", root),
                  patch.object(runtime, "fetch_file", side_effect=corrupt_download)):
                with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                    runtime.source_bundle()
                self.assertFalse((root / "published-v0.8.1-ffmpeg-source.zip").exists())
