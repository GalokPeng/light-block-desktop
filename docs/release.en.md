# Building and distributing the C++ preview

[简体中文](release.md)

The Chinese display label is **测试版（极度优化）** (Preview — Extensive optimization). Always include the separate notice: **unstable, independent installation, does not replace the stable version**. The stable main branch, Python v1.0.0, its tag and archive references remain unchanged. Develop on `cpp-preview`; distribute only GitHub prereleases. Do not merge this branch into main or offer it through the stable update channel.

## Independent identity

`src/version.hpp` is the version source. The display version is `1.1.0-beta.2`; Windows numeric versions are `1.1.0.0`. Subsequent previews increment beta.N or rc.N.

| Item | Preview value |
| --- | --- |
| AppId | `{C07C418A-08FE-4E9B-8136-348822397B6B}` |
| Executable | `轻块桌面测试版.exe` |
| Default installation | `%LOCALAPPDATA%/Programs/Light Block Desktop Preview` |
| Data, startup and desktop-menu namespace | `LightBlockDesktopPreview` |
| Local build | `dist/LightBlockDesktopPreview` |
| Tag | `preview-v1.1.0-beta.2` |
| Fixed preview asset | `Qingkuai-Preview-Setup-x64.exe` |

Setup refuses a directory containing the stable executable, the default stable directory, or a registered stable installation and its descendants. The preview defaults to `data-preview` beside its executable, falling back to `%LOCALAPPDATA%/LightBlockDesktopPreview` if necessary. Uninstall only cleans preview registration and keeps user data. Do not automatically import, modify or delete the stable profile. Migration testing uses copied synthetic fixtures only.

## Build and verification

Use Windows x64, PowerShell 7, Git, Python 3.11+ for build/test scripts, and 7-Zip. Packaging also requires Inno Setup 6. Dependency downloads are pinned and verified within this checkout; neighboring repositories are not required. Users do not need Python or a separate FFmpeg installation.

```powershell
./build.ps1
./tools/test.ps1 -SkipBuild
node --test tests/test_website_downloads.mjs tests/test_website_theme.mjs tests/test_preview_download.mjs
python tools/check_wallpaper_catalog.py
```

Tests use separate profiles and synthetic media. Multi-DPI, fullscreen behavior, Explorer restart, actual screen recording and abnormal shutdown recovery still need interactive checks. Offscreen tests do not establish compatibility with every PC. Never stop or replace a user's active review instance without permission.

Commit the complete candidate before packaging its matching source archive:

```powershell
./tools/release.ps1 -IsccPath 'C:/path/to/ISCC.exe' -SkipBuild -SkipTests
python tools/test_setup.py --iscc 'C:/path/to/ISCC.exe'
python tools/test_update_flow.py --iscc 'C:/path/to/ISCC.exe'
```

Installer tests use temporary AppIds and directories, covering automatic/Chinese/Traditional Chinese/English language selection, preview upgrades, data preservation on uninstall, and refusal to overwrite a stable installation. Helper tests use synthetic Setup files only; the preview UI does not run stable automatic updates. Complete QA of the exact candidate is required before distribution.

## Artifacts and licensing

`preview-release/` contains the versioned `Qingkuai-Preview-v<VERSION>-Setup-x64.exe`, byte-identical `Qingkuai-Preview-Setup-x64.exe`, portable Windows ZIP, application source ZIP, FFmpeg source ZIP, Qt source ZIP and SHA256SUMS. **Never create or upload the stable Qingkuai-Setup-x64.exe alias.** Packaging checks the preview executable identity. Setup has a 128 MiB size budget. Disclose unsigned packages when signing is not configured.

Keep Qt's replaceable DLL notices, LGPL FFmpeg 7.1 decoding sources, and the independent GPL FFmpeg 8.1.3 recording CLI sources distinct. The application's MIT license does not replace dependency licenses.

## Manual prerelease publication

Checks target cpp-preview. Branch/tag pushes build artifacts only. A maintainer must manually run **C++ preview prerelease** with publish=true to publish, always using `--prerelease --latest=false`. Never replace a published asset silently; increment the preview version for fixes.

The website's default download and stable update API must continue to serve the stable product. Add a separate preview entry with a clear instability notice. Verify the prerelease flag, commit, checksums and source assets, and confirm that GitHub's stable latest release and stable update API remain unchanged. A successful push alone is not a completed release.
