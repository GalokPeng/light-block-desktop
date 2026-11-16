# Build and release

[中文完整流程](release.md)

`src/version.hpp` is the only version source. Stable tags are `vX.Y.Z`; Windows resources use `X.Y.Z.0`. The product remains Light Block Desktop.

Keep the original AppId `{3CD16B31-077E-4CE9-BB01-578642D94876}`, executable `轻块桌面.exe`, install directory `Light Block Desktop`, `StillWallpaper` autorun key and fixed installer asset `Qingkuai-Setup-x64.exe`. Upgrade into `data/profile-v2` (or `%LOCALAPPDATA%/StillWallpaper/profile-v2`), with backups and an atomic migration. Never remove old data. Preserve the `python-v1.0.0` branch, `v1.0.0` tag and release assets permanently.

Build with `build.ps1`; run `tools/test.ps1`, website tests, release protocol tests and the catalog checker. Commit reviewed source before `tools/release.ps1`. Run `tools/test_setup.py` and `tools/test_update_flow.py` with Inno Setup. Installer tests must use random AppIds and isolated directories, never a real installation. Real desktop, audio devices and multi-DPI acceptance supplement automated tests.

Pushes to main or stable version tags run the pinned-source pipeline. Only an unpublished newer stable version publishes as latest. The staging branch builds without publication. Release the installer, fixed alias, portable ZIP, application and corresponding dependency sources and SHA256SUMS. Never overwrite an already published version. Setup is currently unsigned; do not claim otherwise.

Verify all published assets and digests, the website download and update JSON, and the preserved v1.0.0 download after publishing. Old preview URLs redirect to the stable website. A successful push alone is not release completion.
