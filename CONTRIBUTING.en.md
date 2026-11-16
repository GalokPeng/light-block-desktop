# Contributing and merging

[简体中文](CONTRIBUTING.md)

The stable `main` branch retains Python v1.0.0. C++ preview work belongs only on `cpp-preview` and must not be merged into main. The same local development and testing process applies to independent forks and upstream contributions.

## Open an upstream pull request

1. Check your remotes and working tree. For a fork, use `origin` for your fork and `upstream` for the original repository. Run `git fetch upstream`, then branch from the intended target (`upstream/main` for stable Python, `upstream/cpp-preview` for the C++ preview) using `fix/<topic>` or `feat/<topic>`. Contributors with upstream write access can branch from an updated `origin/main`.
2. Keep each PR focused on one change. Run `git diff --check`, review the diff and verify compatibility with existing settings. Check new UI text in both languages. Store English translations in [locales/en.json](locales/en.json) with the same format fields as their Chinese source strings.
3. Push your topic branch and open a PR against that same upstream target; never target main with C++ preview work. Describe the issue or goal, scope, tests actually run, Windows and DPI coverage, and any data migration. Use generated data in UI screenshots.
4. The maintainer reviews and merges. Squash merging keeps one upstream commit per PR. Prefer merging the latest upstream `main` into shared branches to preserve their history.

## Release and licensing

PR checks use read-only permissions. The maintainer controls releases. This branch publishes only explicit preview prereleases with `latest=false`, independent identity and separate assets. Stable downloads and update metadata must remain unchanged. See the [Windows release guide](docs/release.en.md).

Contribute code and media you can license under the repository's [MIT license](LICENSE). Document the source and license of third-party material. Keep personal desktop content, clipboard history, notes, credentials and build output out of Git.

Main retains Python v1.0.0. Preserve it, the `python-v1.0.0` archive branch and the original tag. C++23 / Qt / Win32 changes belong exclusively to `cpp-preview`. Run `build.ps1` and `tools/test.ps1 -SkipBuild` using isolated data.
