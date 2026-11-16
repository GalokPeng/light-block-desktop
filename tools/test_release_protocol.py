"""Offline tests for the separate preview distribution contract."""
from pathlib import Path
from tempfile import TemporaryDirectory
import json
import os
import unittest
from unittest.mock import patch
import zipfile
import release


class PreviewReleaseContract(unittest.TestCase):
    def test_only_prerelease_versions_are_accepted(self):
        self.assertEqual(release.windows_version("1.1.0-beta.1"), "1.1.0.0")
        for value in ("1.1.0", "v1.1.0-beta.1", "1.1.0-beta.0", "1.1.0-beta.01", "65536.1.0-beta.1"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                release.components(value)

    def test_preview_progression_does_not_consume_stable_tags(self):
        self.assertTrue(release.should_publish("1.1.0-beta.1", ["v1.0.0", "v9.0.0"]))
        self.assertFalse(release.should_publish("1.1.0-beta.1", ["preview-v1.1.0-beta.1"]))
        with self.assertRaises(ValueError):
            release.should_publish("1.1.0-beta.1", ["preview-v1.1.0-beta.2"])
        self.assertTrue(release.should_publish("1.1.0-rc.1", ["preview-v1.1.0-beta.99"]))

    def test_official_alias_and_incomplete_assets_are_rejected(self):
        version = "1.1.0-beta.1"
        with TemporaryDirectory(prefix="preview-release-contract-") as folder:
            output = Path(folder)
            names = [f"{release.prefix(version)}-{suffix}" for suffix in ("Setup-x64.exe", "Windows-x64.zip", "source.zip", "ffmpeg-source.zip", "qt-source.zip")]
            names.append(release.PREVIEW_ALIAS)
            for name in names:
                (output / name).write_bytes(b"preview fixture")
            portable = release.prefix(version) + "-Windows-x64"
            with zipfile.ZipFile(output / (portable + ".zip"), "w") as archive:
                archive.writestr(portable + "/build-info.json", json.dumps(dict(version=version, channel="preview", installer_app_id=release.PREVIEW_APP_ID, source_dirty=False, source_commit="a" * 40)))
            manifest = output / f"{release.prefix(version)}-SHA256SUMS.txt"
            manifest.write_text("".join(f"{release.digest(output / name)}  {name}\n" for name in names), encoding="ascii")
            release.verify_release(version, output, expected_commit="a" * 40)
            with self.assertRaises(ValueError):
                release.verify_release(version, output, expected_commit="b" * 40)
            (output / "Qingkuai-Setup-x64.exe").write_bytes(b"must never be distributed by preview")
            with self.assertRaises(ValueError):
                release.verify_release(version, output)
            (output / "Qingkuai-Setup-x64.exe").unlink()
            manifest.write_text(manifest.read_text(encoding="ascii").splitlines()[0] + "\n", encoding="ascii")
            with self.assertRaises(ValueError):
                release.verify_release(version, output)

    def test_source_identity_matches_isolated_installer(self):
        version = release.read_version()
        self.assertIn("-", version)
        installer = (release.ROOT / "installer/LightBlockDesktop.iss").read_text(encoding="utf-8-sig")
        self.assertIn("AppId={" + release.PREVIEW_APP_ID, installer)
        self.assertIn("DefaultDirName={localappdata}\\Programs\\Light Block Desktop Preview", installer)
        self.assertNotEqual(release.EXE_NAME, "轻块桌面.exe")
        self.assertIn(release.EXE_NAME, installer)

    def test_ci_requires_preview_ref_and_exact_trigger_sha(self):
        version, sha = "1.1.0-beta.1", "a" * 40
        def fake_git(*args):
            return {("rev-parse", "HEAD"): sha, ("branch", "--show-current"): ""}[args]
        env = dict(GITHUB_ACTIONS="true", GITHUB_REF="refs/heads/cpp-preview", GITHUB_SHA=sha)
        with patch.object(release, "git", fake_git), patch.dict(os.environ, env, clear=True):
            self.assertEqual(release.verify_source_ref(version), sha)
            os.environ["GITHUB_SHA"] = "b" * 40
            with self.assertRaises(ValueError):
                release.verify_source_ref(version)
            os.environ["GITHUB_SHA"] = sha
            os.environ["GITHUB_REF"] = "refs/heads/main"
            with self.assertRaises(ValueError):
                release.verify_source_ref(version)
            os.environ["GITHUB_REF"] = "refs/tags/preview-v" + version
            self.assertEqual(release.verify_source_ref(version), sha)

    def test_every_release_checkout_pins_trigger_sha(self):
        text = (release.ROOT / ".github/workflows/release.yml").read_text(encoding="utf-8")
        self.assertNotIn("ref: ${{ github.ref }}", text)
        blocks = text.split("- uses: actions/checkout@")[1:]
        self.assertEqual(len(blocks), 3)
        for block in blocks:
            self.assertIn("ref: ${{ github.sha }}", block.split("      - ", 1)[0])
        self.assertIn("--prerelease --latest=false", text)


if __name__ == "__main__":
    unittest.main()
