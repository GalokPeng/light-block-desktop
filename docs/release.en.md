# Build and release for Windows

[简体中文](release.md)

`VERSION` in `still/model.py` is the single release version. It drives the executable metadata, portable directory, Setup filename and GitHub tag.

## Local verification

Use Windows x64, Python 3.11, Git and [Inno Setup 6](https://jrsoftware.org/isdl.php). Commit the exact source you want to package so the binary and source ZIP come from the same commit.

```powershell
python -m pip install -r requirements.txt
python tools/run_tests.py
# Run in an interactive Windows session for native Explorer tests
python tools/run_tests.py --desktop
# Run from the repository root. Use -IsccPath for a different compiler location.
.\tools\release.ps1
```

`release/` contains Setup, a portable ZIP, the source ZIP and SHA-256 checksums. Setup is unsigned and installs for the current user by default. It preserves settings on upgrade and uninstall. If you switch from portable to installed, toggle **Start with Windows** to update its path.

The first build downloads SHA-256-verified LLVM/MinGW-w64 and C++/WinRT build tools. Setup and the portable archive include the native taskbar DLL. The installation checks verify that the DLL loads. The three-section layout requires Windows 11 22H2 or newer on x64.

Setup selects English, Simplified Chinese or Traditional Chinese using the Windows display language. The app uses Simplified Chinese for Chinese display languages and English otherwise. On a Windows test machine without an existing installation, run `python tools/test_setup.py` to check language selection, startup, uninstall and config preservation in temporary directories.

Recording and export require a separate FFmpeg build on PATH with `gdigrab`, `libx264`, `ffv1` and palette filters. Static, GIF and video wallpapers use the bundled runtime.

## First GitHub release

1. Push `main` to an empty GitHub repository.
2. Run **Windows release** from Actions with `publish` disabled. Download and verify the artifacts.
3. Set the Actions repository variable `PUBLISH_RELEASES` to `true`. Dispatch **Windows release** with `publish` enabled. The workflow uses GitHub's built-in token for publication.

## Subsequent releases

Update `CHANGES.md` and increment `VERSION`. Push the commit to `main`, or merge the release PR into `main`. GitHub Actions runs the Windows tests, builds the assets and checks Setup. It then tags the triggering commit and publishes the release. Existing version tags skip publication.

If a run stops during publication, check the Actions log and Releases for a partial draft. Keep existing tags and assets. The maintainer can complete the draft for the same commit or increment the version for a fresh release.

The [product site](website.md) deploys separately through Vercel's GitHub integration. Its download link tracks the latest GitHub Release.
