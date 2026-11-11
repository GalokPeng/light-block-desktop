import copy
from hashlib import sha1, sha256
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
from tempfile import TemporaryDirectory
from threading import Thread
import time
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

from PySide6.QtCore import QEventLoop, QTimer, QUrl
from PySide6.QtNetwork import QNetworkAccessManager, QNetworkRequest
from PySide6.QtWidgets import QApplication, QMainWindow

from still.app import Controller
from still.model import ASSETS, Store, item_name
from still.online_catalog import (MEDIA_HOSTS, SEED, checksum_key, https_url, local_id, parse_catalog)
from still.online_wallpapers import GifCoverFrame, OnlineLibrary


class OnlineWallpaperTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])

    def setUp(self):
        self.temporary = TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.store = Store(Path(self.temporary.name))
        template = copy.deepcopy(parse_catalog(SEED.read_bytes())["entries"][0])
        template.pop("sha1", None)
        template["sha256"] = "0" * 64
        self.catalog = {"schema_version": 4, "catalog_revision": 1, "entries": []}
        # Synthetic small fixtures are independent of which real works are curated.
        for ident, zh, en, kind, ext in (("commons-fronalpstock", "瑞士山湖", "Swiss Alpine Lake", "image", ".png"),
                ("commons-fractal-canopy", "分形生长", "Fractal Canopy", "gif", ".gif"),
                ("commons-small-waterfall", "林间瀑布", "Forest Waterfall", "video", ".mp4"),
                ("commons-northern-lights", "极光延时", "Northern Lights Aurora", "gif", ".gif")):
            entry = dict(template, id=ident, title={"zh": zh, "en": en}, tags=[zh, en], kind=kind, filename=ident+ext)
            if kind == "gif":
                entry.update(width=1920, height=1080, frame_count=2, looped=True, duration_seconds=1)
            self.catalog["entries"].append(entry)
        self.entry = copy.deepcopy(self.catalog["entries"][0])
        self.body = (ASSETS / "night-drive.jpg").read_bytes()
        self.entry.update(filename="original.png", bytes=len(self.body), sha256=sha256(self.body).hexdigest())
        self.served, self.requests, self.slow, self.status = self.body, [], False, 200
        self.redirect = None
        self.redirect_once = False
        fixture = self

        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                fixture.requests.append(self.path)
                if fixture.redirect and not (fixture.redirect_once and len(fixture.requests) > 1):
                    self.send_response(302)
                    self.send_header("Location", fixture.redirect)
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    return
                data = fixture.served
                self.send_response(fixture.status)
                self.send_header("Content-Length", str(len(data)))
                if fixture.status == 429:
                    self.send_header("Retry-After", "120")
                self.end_headers()
                try:
                    for index in range(0, len(data), 1024):
                        self.wfile.write(data[index:index+1024])
                        self.wfile.flush()
                        if fixture.slow:
                            time.sleep(.01)
                except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                    pass
            def log_message(self, *_args):
                pass
        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.local_redirect_url = f"http://127.0.0.1:{server.server_port}/original.png"
        Thread(target=server.serve_forever, daemon=True).start()
        self.addCleanup(server.server_close)
        self.addCleanup(server.shutdown)

        class Network(QNetworkAccessManager):
            def createRequest(self, operation, request, outgoing=None):
                request = QNetworkRequest(request)
                request.setUrl(QUrl(f"http://127.0.0.1:{server.server_port}" + request.url().path()))
                return super().createRequest(operation, request, outgoing)

        self.network = Network()
        self.library = OnlineLibrary(self.store, network=self.network)
        self.library.catalog = copy.deepcopy(self.catalog)
        self.addCleanup(self.library.close)

    def spin(self, condition, timeout=6):
        deadline = time.monotonic() + timeout
        while not condition() and time.monotonic() < deadline:
            loop = QEventLoop()
            QTimer.singleShot(10, loop.quit)
            loop.exec()
        self.assertTrue(condition(), "Timed out waiting for asynchronous wallpaper operation")

    def test_loading_seed_makes_no_requests_and_offline_catalog_does_not_erase_it(self):
        self.assertEqual(self.requests, [])
        self.status, self.served = 404, b"not found"
        self.library.activate()
        self.spin(lambda: self.library.catalog_state == "offline")
        self.assertEqual(self.library.entries, self.catalog["entries"])

    def test_refresh_retries_failed_covers_even_when_the_remote_catalog_is_unavailable(self):
        self.library.thumb_failed.add(self.entry["id"])
        self.status, self.served = 404, b"missing catalog"
        self.library.refresh()
        self.assertNotIn(self.entry["id"], self.library.thumb_failed)
        self.spin(lambda: self.library.catalog_state == "offline")
        self.status, self.served = 200, self.body
        self.library.thumbnails_active = True
        self.library.thumbnail(self.entry)
        self.spin(lambda: self.library.thumbnail_path(self.entry).is_file())

    def test_catalog_rejects_unsafe_paths_unreviewed_hosts_licenses_and_duplicates(self):
        for field, value in (("filename", "../outside.png"), ("filename", "C:outside.jpg"), ("filename", "CON.png"),
                             ("download_url", "https://upload.wikimedia.org.evil.example/a.jpg"),
                             ("download_url", "http://upload.wikimedia.org/a.jpg"),
                             ("license", "free download"), ("bytes", 2**40), ("sha256", "")):
            with self.subTest(field=field, value=value):
                catalog = copy.deepcopy(self.catalog)
                catalog["entries"][0][field] = value
                with self.assertRaises(ValueError):
                    parse_catalog(json.dumps(catalog).encode())
        duplicate = copy.deepcopy(self.catalog)
        duplicate["entries"].append(duplicate["entries"][0])
        with self.assertRaises(ValueError):
            parse_catalog(json.dumps(duplicate).encode())

    def test_download_preserves_original_bytes_and_attribution_and_survives_restart(self):
        self.library.download(self.entry)
        self.spin(lambda: not self.library.busy)
        self.assertEqual(self.library.state(self.entry), "downloaded")
        item = self.library.local_item(self.entry)
        self.assertEqual(self.store.path(item).read_bytes(), self.body)
        self.assertEqual(json.loads((self.store.path(item).parent / "origin.json").read_text(encoding="utf-8")), self.entry)
        restarted = OnlineLibrary(Store(self.store.directory), network=self.network)
        self.addCleanup(restarted.close)
        self.assertEqual(restarted.state(self.entry), "downloaded")
        with patch("still.i18n.display_language", return_value="en"):
            self.assertEqual(item_name(item), self.entry["title"]["en"])
        count = len(self.requests)
        self.library.download(self.entry)
        self.assertEqual(len(self.requests), count)

    def test_bad_hash_and_html_response_never_enter_library(self):
        for data in (b"<html>error</html>", self.body[:-1] + bytes([self.body[-1] ^ 1])):
            with self.subTest(response=data[:10]):
                self.served = data
                self.library.download(self.entry)
                self.spin(lambda: not self.library.busy)
                self.assertNotEqual(self.library.state(self.entry), "downloaded")
                self.assertIsNone(self.store.find(local_id(self.entry)))
                self.assertEqual(list(self.store.directory.rglob("*.part")), [])

    def test_cancel_during_stream_removes_partial_data_and_allows_retry(self):
        self.slow = True
        self.library.download(self.entry)
        self.spin(lambda: self.library.progress.get(self.entry["id"], (0, 0))[0] > 0)
        self.library.cancel_download(self.entry)
        self.assertFalse(self.library.busy)
        self.assertEqual(self.library.state(self.entry), "available")
        self.assertEqual(list(self.store.directory.rglob("*.part")), [])
        self.slow = False
        self.library.download(self.entry)
        self.spin(lambda: not self.library.busy)
        self.assertEqual(self.library.state(self.entry), "downloaded")

    def test_missing_modified_deleted_and_revised_files_are_downloadable_again(self):
        self.library.download(self.entry)
        self.spin(lambda: not self.library.busy)
        item = self.library.local_item(self.entry)
        revised = dict(self.entry, revision=2)
        self.assertEqual(self.library.state(revised), "update")
        self.store.path(item).write_bytes(b"changed")
        self.assertEqual(self.library.state(self.entry), "available")
        self.store.remove(item["id"])
        self.assertFalse(self.store.path(item).parent.exists())
        self.library.download(self.entry)
        self.spin(lambda: not self.library.busy)
        self.store.path(self.library.local_item(self.entry)).unlink()
        self.assertEqual(self.library.state(self.entry), "available")

    def test_failed_library_save_rolls_back_download_and_origin(self):
        with patch.object(self.store, "save", side_effect=OSError("disk full")):
            self.library.download(self.entry)
            self.spin(lambda: not self.library.busy)
        self.assertEqual(self.library.state(self.entry), "disk")
        self.assertIsNone(self.store.find(local_id(self.entry)))
        self.assertEqual(list((self.store.directory / "wallpapers").rglob("original.png")), [])
        self.assertEqual(list((self.store.directory / "wallpapers").rglob("origin.json")), [])

    def test_invalid_remote_catalog_preserves_last_valid_cache(self):
        cache = self.library.directory / "catalog.json"
        cache.parent.mkdir(parents=True)
        cache.write_text(json.dumps(self.catalog), encoding="utf-8")
        self.served = b'{"schema_version":999,"entries":[]}'
        self.library.refresh()
        self.spin(lambda: self.library.catalog_state == "offline")
        self.assertEqual(parse_catalog(cache.read_bytes()), self.catalog)

    def test_valid_remote_catalog_is_cached_and_loaded_without_another_request(self):
        updated = copy.deepcopy(self.catalog)
        updated["entries"][0]["title"]["en"] = "Updated title"
        self.served = json.dumps(updated).encode()
        self.library.refresh()
        self.spin(lambda: self.library.catalog_state == "live")
        self.assertEqual(self.library.entries[0]["title"]["en"], "Updated title")
        count = len(self.requests)
        restarted = OnlineLibrary(self.store, network=self.network)
        self.addCleanup(restarted.close)
        self.assertEqual(restarted.catalog_state, "cached")
        self.assertEqual(restarted.catalog, updated)
        self.assertEqual(len(self.requests), count)

    def test_redirect_to_an_unreviewed_host_stops_before_fetching_it(self):
        self.redirect = "https://unreviewed.invalid/original.png"
        self.library.download(self.entry)
        self.spin(lambda: not self.library.busy)
        self.assertEqual(self.library.state(self.entry), "source")
        self.assertEqual(len(self.requests), 1)
        self.assertEqual(list(self.store.directory.rglob("*.part")), [])

    def test_verified_redirect_emits_the_qt_permission_signal(self):
        self.redirect = self.local_redirect_url
        self.redirect_once = True
        # The local HTTP fixture is permitted only in this test. Production
        # allowlist behavior, including CDN lookalikes, is tested separately.
        with patch("still.online_wallpapers.https_url", return_value=True):
            self.library.download(self.entry)
            self.spin(lambda:not self.library.busy)
        self.assertEqual(self.library.state(self.entry), "downloaded")
        self.assertEqual(len(self.requests), 2)

    def test_valid_checksum_for_a_non_media_response_still_fails(self):
        self.served = b"<html>upstream error</html>"
        entry = dict(self.entry, bytes=len(self.served), sha256=sha256(self.served).hexdigest())
        self.library.download(entry)
        self.spin(lambda: not self.library.busy)
        self.assertEqual(self.library.state(entry), "integrity")
        self.assertIsNone(self.library.local_item(entry))

    def test_at_most_two_original_downloads_run_at_once(self):
        entries = [dict(self.entry, id="test-wallpaper-" + str(index)) for index in range(3)]
        for entry in entries:
            self.library.download(entry)
        self.assertEqual(sum(key[0] == "download" for key in self.library.jobs), 2)
        self.assertEqual(self.library.state(entries[2]), "limit")
        self.library.cancel_download(entries[0])
        self.library.download(entries[2])
        self.assertEqual(sum(key[0] == "download" for key in self.library.jobs), 2)

    def test_preview_is_reused_and_reverified_for_download_without_a_second_request(self):
        self.library.preview_media(self.entry)
        self.spin(lambda: self.library.has_preview(self.entry))
        count = len(self.requests)
        self.library.preview_media(self.entry)
        self.assertEqual(len(self.requests), count)
        self.library.download(self.entry)
        self.spin(lambda: not self.library.busy)
        self.assertEqual(self.library.state(self.entry), "downloaded")
        self.assertEqual(len(self.requests), count)
        self.assertEqual(self.store.path(self.library.local_item(self.entry)).read_bytes(), self.body)

    def test_damaged_preview_cache_is_not_imported_and_retry_uses_the_source(self):
        path = self.library.preview_path(self.entry)
        path.parent.mkdir(parents=True)
        path.write_bytes(self.body[:-1] + bytes([self.body[-1] ^ 1]))
        self.library.download(self.entry)
        self.spin(lambda: not self.library.busy)
        self.assertEqual(self.library.state(self.entry), "integrity")
        self.assertEqual(self.requests, [])
        self.library.download(self.entry)
        self.spin(lambda: not self.library.busy)
        self.assertEqual(self.library.state(self.entry), "downloaded")
        self.assertEqual(len(self.requests), 1)

    def test_cached_file_copy_can_be_cancelled_without_leaving_partial_files(self):
        path = self.library.preview_path(self.entry)
        path.parent.mkdir(parents=True)
        path.write_bytes(self.body)
        self.library.download(self.entry)
        self.assertTrue(self.library.busy)
        self.library.cancel_download(self.entry)
        self.assertFalse(self.library.busy)
        self.assertEqual(list(self.store.directory.rglob("*.part")), [])
        self.assertIsNone(self.library.local_item(self.entry))

    def test_rate_limited_source_is_not_requested_again_during_retry_after(self):
        self.status, self.served = 429, b"rate limited"
        self.library.download(self.entry)
        self.spin(lambda: not self.library.busy)
        self.assertEqual(self.library.state(self.entry), "rate_limit")
        self.assertTrue(self.library.limited(self.entry["download_url"]))
        self.library.download(self.entry)
        self.spin(lambda: self.library.state(self.entry) != "downloading")
        self.assertEqual(len(self.requests), 1)

    def test_online_page_filters_both_languages_and_cancels_preview_on_hiding(self):
        from still.online_ui import OnlinePage
        window = QMainWindow()
        window.light, window.refresh_cards = False, Mock()
        window.select, window.apply_selected = Mock(), Mock()
        page = OnlinePage(window, self.library)
        window.setCentralWidget(page)
        window.resize(1050, 700)
        # Avoid external requests while testing the actual page and its signals.
        self.library.activated = True
        self.library.thumbnail = Mock(return_value=None)
        window.show()
        page.toggle_preview()
        self.assertIs(page.browse_stack.currentWidget(), page.preview_stack)
        page.close_preview()
        self.assertIs(page.browse_stack.currentWidget(), page.view)
        page.set_filter("gif")
        page.search.setText("aurora")
        self.assertEqual(page.model.rowCount(), 1)
        self.assertEqual(page.selected["id"], "commons-northern-lights")
        page.set_filter("image")
        page.search.setText("山湖")
        self.assertEqual(page.model.rowCount(), 1)
        page.set_filter("gif")
        self.assertEqual(page.model.rowCount(), 0)
        page.search.clear()
        self.assertEqual(page.model.rowCount(), 2)
        page.playing_id = page.selected["id"]
        window.hide()
        self.assertEqual(page.playing_id, "")
        self.assertFalse(self.library.thumbnails_active)
        window.close()
        window.deleteLater()

    def test_pending_download_keeps_controller_alive_after_settings_close(self):
        controller = SimpleNamespace(engine=SimpleNamespace(surface=None), store=self.store,
            online_library=SimpleNamespace(busy=True), release_check=None, native_taskbar_style=None,
            hotkeys=None, workbench=None)
        self.assertTrue(Controller.needs_background(controller))

    def test_source_sha1_download_and_preview_cache_use_the_recorded_algorithm(self):
        entry = dict(self.entry)
        entry.pop("sha256")
        entry["sha1"] = sha1(self.body).hexdigest()
        self.library.preview_media(entry)
        self.spin(lambda: self.library.has_preview(entry))
        count = len(self.requests)
        self.library.download(entry)
        self.spin(lambda: not self.library.busy)
        self.assertEqual(self.library.state(entry), "downloaded")
        self.assertEqual(len(self.requests), count)
        self.assertTrue(checksum_key(entry).startswith("sha1-"))
        self.assertEqual(self.store.path(self.library.local_item(entry)).read_bytes(), self.body)

    def test_gif_cover_stops_after_first_frame_even_with_a_large_declared_body(self):
        from PySide6.QtGui import QImage
        original = (ASSETS / "startrail.gif").read_bytes()
        self.served = original + b"\x00" * (9 * 1024 * 1024)
        self.slow = True
        self.library.thumbnails_active = True
        self.library.thumbnail(self.entry)
        target = self.library.thumbnail_path(self.entry)
        self.spin(target.is_file)
        cover = target.read_bytes()
        self.assertLess(len(cover), len(original))
        self.assertEqual(QImage.fromData(cover), QImage.fromData(original))
        self.assertEqual(list(target.parent.glob("*.part")), [])

    def test_retired_three_hundred_item_cache_cannot_replace_curated_seed(self):
        old = dict(self.catalog, schema_version=2)
        old["entries"] = [dict(self.catalog["entries"][0], id=f"retired-{i}", categories=["landscape"]) for i in range(300)]
        cache = self.library.directory / "catalog.json"
        cache.parent.mkdir(parents=True)
        cache.write_text(json.dumps(old), encoding="utf-8")
        restarted = OnlineLibrary(self.store, network=self.network)
        self.addCleanup(restarted.close)
        self.assertEqual(restarted.entries, parse_catalog(SEED.read_bytes())["entries"])
        self.served = json.dumps(old).encode()
        self.library.refresh()
        self.spin(lambda: self.library.catalog_state == "offline")
        self.assertEqual(self.library.entries, self.catalog["entries"])

    def test_older_same_schema_cache_and_remote_cannot_remove_restored_gifs(self):
        current = parse_catalog(SEED.read_bytes())
        old = copy.deepcopy(current)
        old.pop("catalog_revision", None)
        old["entries"] = [e for e in old["entries"] if e["kind"] != "gif"]
        cache = self.library.directory / "catalog.json"
        cache.parent.mkdir(parents=True)
        cache.write_text(json.dumps(old), encoding="utf-8")
        restarted = OnlineLibrary(self.store, network=self.network)
        self.addCleanup(restarted.close)
        self.assertEqual(restarted.entries, current["entries"])
        restarted.catalog = current
        self.served = json.dumps(old).encode()
        restarted.refresh()
        self.spin(lambda: restarted.catalog_state == "offline")
        self.assertEqual(sum(e["kind"] == "gif" for e in restarted.entries), 10)

    def test_only_three_media_filters_remain_and_search_is_scoped_to_current_type(self):
        from still.online_ui import OnlinePage
        window = QMainWindow()
        window.light, window.refresh_cards = False, Mock()
        self.library.activated = True
        self.library.thumbnail = Mock(return_value=None)
        page = OnlinePage(window, self.library)
        window.setCentralWidget(page)
        self.assertFalse(hasattr(page, "content_category"))
        self.assertEqual(set(page.kind_buttons), {"image", "gif", "video"})
        self.assertEqual(page.model.rowCount(), 1)
        page.set_filter("gif")
        self.assertEqual(page.model.rowCount(), 2)
        page.search.setText("aurora")
        self.assertEqual(page.model.rowCount(), 1)
        page.search.setText("not-a-match")
        self.assertEqual(page.model.rowCount(), 0)
        self.library.catalog["entries"] = [e for e in self.library.entries if e["kind"] != "gif"]
        page.search.clear()
        page.refresh_state()
        self.assertEqual(page.model.rowCount(), 0)
        self.assertIn("1080p", page.empty.text())
        from still.i18n import tr
        self.assertEqual(page.download_button.text(), tr("下载原文件"))
        window.close()
        window.deleteLater()

    def test_pre_size_limit_catalog_is_not_restored_from_cache_or_remote(self):
        old = copy.deepcopy(self.catalog)
        old["schema_version"] = 3
        old["entries"][0]["bytes"] = 21_000_000
        cache = self.library.directory / "catalog.json"
        cache.parent.mkdir(parents=True)
        cache.write_text(json.dumps(old), encoding="utf-8")
        restarted = OnlineLibrary(self.store, network=self.network)
        self.addCleanup(restarted.close)
        self.assertEqual(restarted.catalog["schema_version"], 4)
        self.assertTrue(all(e["bytes"]<=20_000_000 for e in restarted.entries))
        self.served = json.dumps(old).encode()
        self.library.refresh()
        self.spin(lambda:self.library.catalog_state == "offline")
        self.assertEqual(self.library.catalog["schema_version"], 4)

    def test_smaller_preview_is_never_promoted_as_the_original_download(self):
        original = b"\x00\x00\x00\x18ftypisom" + b"original" * 1024
        preview = b"\x00\x00\x00\x18ftypisom" + b"preview" * 20
        entry = dict(self.entry, kind="video", filename="original.mp4", bytes=len(original), sha256=sha256(original).hexdigest(),
            preview={"filename": "preview-sample.mp4", "download_url": self.entry["download_url"], "bytes": len(preview), "sha1": sha1(preview).hexdigest()})
        self.served = preview
        self.library.preview_media(entry)
        self.spin(lambda: self.library.has_preview(entry))
        self.served = original
        self.library.download(entry)
        self.spin(lambda:not self.library.busy)
        item = self.library.local_item(entry)
        self.assertEqual(self.store.path(item).read_bytes(), original)
        self.assertEqual(len(self.requests), 2)
        ready = Mock()
        self.library.preview_ready.connect(ready)
        self.library.preview_media(entry)
        ready.assert_called_once_with(entry["id"], str(self.store.path(item)))


class CuratedCatalogTests(unittest.TestCase):
    def test_first_gif_frame_parser_handles_chunk_boundaries_and_local_palette(self):
        from PySide6.QtGui import QImage
        original = (ASSETS / "startrail.gif").read_bytes()
        for chunk_size in (1, 7, 1024):
            parser = GifCoverFrame()
            for start in range(0, len(original), chunk_size):
                result = parser.feed(original[start:start+chunk_size])
                if result:
                    break
            self.assertEqual(QImage.fromData(result), QImage.fromData(original))
            self.assertLess(len(result), len(original))
        parser = GifCoverFrame()
        self.assertIsNone(parser.feed(b"\x89PNG\r\n\x1a\n"))
        self.assertTrue(parser.not_gif)

    def test_curated_hd_originals_have_review_evidence_and_no_categories(self):
        from collections import Counter
        catalog = parse_catalog(SEED.read_bytes())
        counts = Counter(e["kind"] for e in catalog["entries"])
        self.assertEqual(counts["image"], 20)
        self.assertEqual(counts["video"], 20)
        self.assertLessEqual(counts["gif"], 20)
        self.assertEqual(catalog["schema_version"], 4)
        keys = [checksum_key(e) for e in catalog["entries"]]
        self.assertEqual(len(keys), len(set(keys)))
        urls = [e["download_url"] for e in catalog["entries"]]
        self.assertEqual(len(urls), len(set(urls)))
        for entry in catalog["entries"]:
            self.assertNotIn("categories", entry)
            self.assertGreaterEqual(entry["width"], 1920)
            self.assertGreaterEqual(entry["height"], 1080)
            self.assertLessEqual(entry["bytes"], 20_000_000)
            if entry["kind"] == "gif":
                self.assertEqual(entry["width"] * 9, entry["height"] * 16)
                self.assertGreaterEqual(entry["frame_count"], 2)
                self.assertIs(entry["looped"], True)
            else:
                self.assertTrue(any((entry["popularity"].get(k) or 0)>0 for k in ("votes", "likes", "favorites")))

    def test_gifs_allow_unpublished_popularity_but_require_verified_looping_16_by_9_frames(self):
        catalog = parse_catalog(SEED.read_bytes())
        gif = dict(copy.deepcopy(catalog["entries"][0]), id="verified-gif-fixture", kind="gif", filename="loop.gif",
                   width=1920, height=1080, frame_count=12, looped=True, duration_seconds=1)
        value = copy.deepcopy(catalog)
        value["entries"] = [gif]
        index = 0
        value["entries"][index]["popularity"].update(views=None, likes=None, favorites=None, votes=None)
        parse_catalog(json.dumps(value).encode())
        for change in ({"width":1920,"height":1200}, {"frame_count":1}, {"frame_count":True},
                       {"looped":False}, {"duration_seconds":0}, {"duration_seconds":121}):
            invalid = copy.deepcopy(value)
            invalid["entries"][index].update(change)
            with self.subTest(change=change), self.assertRaises(ValueError):
                parse_catalog(json.dumps(invalid).encode())

    def test_reject_low_resolution_unsubstantiated_popularity_and_category_fields(self):
        catalog = parse_catalog(SEED.read_bytes())
        for change in ({"width":1919}, {"height":1079}, {"bytes":20_000_001}, {"categories":["landscape"]}, {"kind":None,"filename":"unsupported.exe"},
                {"popularity":{"evidence_url":"https://example.com", "views":10000000, "checked_at":"2026-11-11"}}):
            value = copy.deepcopy(catalog)
            value["entries"][0].update(change)
            with self.assertRaises(ValueError):
                parse_catalog(json.dumps(value).encode())

    def test_archive_redirect_allowlist_rejects_host_lookalikes_and_http(self):
        self.assertTrue(https_url("https://ia801234.us.archive.org/34/items/example/a.mp4", MEDIA_HOSTS))
        self.assertTrue(https_url("https://ia801234.archive.org/example/a.mp4", MEDIA_HOSTS))
        self.assertTrue(https_url("https://dn711106.ca.archive.org/0/items/example/a.mp4", MEDIA_HOSTS))
        self.assertTrue(https_url("https://dn800301.us.archive.org/0/items/example/a.wmv", MEDIA_HOSTS))
        for url in ("https://ia801234.us.archive.org.evil.example/a.mp4", "https://dn711106.ca.archive.org.evil.example/a.mp4", "https://evil.archive.org/a.mp4", "http://archive.org/a.mp4"):
            self.assertFalse(https_url(url, MEDIA_HOSTS))
