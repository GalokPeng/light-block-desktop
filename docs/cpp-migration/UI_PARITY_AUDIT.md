# Python 1.0.0 UI parity audit

> **Historical local-migration audit from the independent test phase.** “Current”, fixture paths, screenshots and the adjacent Python checkout below refer to that isolated experiment. They are not preview-release acceptance evidence. Stable Python 1.0.0 remains on main; the preview does not replace its website or update channel. Python remains archived at [`python-v1.0.0`](https://github.com/GalokPeng/light-block-desktop/tree/python-v1.0.0) and `v1.0.0`. Follow the [current release checks](../release.en.md) and [upgrade guide](../usage.en.md#upgrades-and-data). This ledger preserves observed differences and does not claim pixel-perfect parity.

## Preview branch follow-up (2026-11-13)

The independent **1.1.0-beta.1 / 测试版（极度优化）** work now adds left-aligned collapsible section headings with separate right-hand switches, a Small/Medium/Large organizer slider, DPI-dependent shelf geometry and bounded hover enlargement, friendly monitor names with resolution/scale and one primary entry, page-specific headings, compact theme/language controls, and the original-style About composition with its GitHub mark. Media studio is an embedded tool page rather than a separate settings window. Settings remain immediate; formal Python profiles and PID 4968 were not touched.

File workbench now requests real Shell icons through a separate bounded native worker and an identity-checked ABI 2 callback. It requests only visible rows, bounds pending work to 48 and caches to 128 icons per side. Returned pixels are copied before the caller's buffer is reused; a render review caught and fixed an earlier temporary-buffer ownership defect. Closing the page does not wait for a blocked Shell extension. Default organizer icon-worker behavior is unchanged. The exact-metadata lookup, GDI flush and copied pixels are included in the linked-binary fixtures below.

The refresh command now has a validated remote catalog service; failed refreshes preserve the prior catalog and active media transfers prevent replacement. The old refresh-only-covers finding below describes the earlier build, not this branch. Live network media acceptance remains separate and no local network restriction was bypassed.

UI evidence uses isolated synthetic profiles and hidden rendering, not the user's desktop:

| Actual Qt device ratio | Local report directory | Result |
| --- | --- | --- |
| 1.0 | `.local/appearance-dpi-100/screens` | All UI assertions passed |
| 1.5 | `.local/appearance-final-dpi100/screens` | All UI assertions passed; the directory name predates ratio calibration |
| 2.0 | `.local/appearance-dpi-200/screens` | All UI assertions passed |
| 2.0, after icon-buffer fix | `.local/appearance-icons-fixed-dpi200/screens` | All UI assertions plus exact icon-pixel ownership passed |

Each run renders Chinese/English and dark/light settings, Appearance, About and Media studio, and checks title/toggle alignment, discrete icon size, button contents, CJK glyph availability, page titles and 16 distinct scroll positions. Hotkey round trips cover Win/Meta, all keypad keys and punctuation requiring implicit Shift/Ctrl/Alt. The final UI DLL for this scoped run was `LightBlockUi-e726b8be915c.dll` (SHA-256 `e726b8be915c54915fa9556930b8252d938d0c909994718cd082ced9a5633c67`), with native executable `fcb66f80d1f86a8176e08eb9a0a2ded29d6e02a2776cbed74e35cc68155efc38`. Parent integration added further native changes afterward; these reports do not replace the final package checks.

Reviewed renders showed no clipped theme/language controls or missing Chinese headings. Long monitor names elide with a full tooltip. Media-studio renders here are empty-source forms, not live export evidence. Real mixed-monitor transitions, organizer/taskbar takeover, Explorer restarts, IME and animation smoothness still need practical acceptance; geometry and screenshot checks are not pixel-perfect proof.

## Unified settings and preview follow-up (2026-11-14)

Settings rows now use a left label and a consistent right-hand control column, a minimum 64 logical-pixel height, and a subtle divider. Pointer size, particle style and both colors follow the same layout. Display/fit options remain on Wallpapers only. Media studio remains embedded and is entered from Capture; the duplicate Tools card has been removed. Local and online wallpaper grids use three columns, including after a scrollbar appears. The grid accounts for Qt's internal style scrollbar reservation, which can differ from its painted QSS width.

The local inspector again shows type, image/GIF dimensions, file size and extension. Missing/unreadable sources disable Apply. Metadata and still-image decoding run off the UI thread with one active task and one replaceable latest request; stale/cancelled requests cannot update another selection. The same bounded loader handles downloaded online still previews. This does not change source-file quality or move media playback into the UI thread. Local thumbnail copies are now bounded to 24 entries; the native cache is independently bounded by the backend.

Applying a wallpaper stops its preview first. The same GIF does not automatically reopen a second decoder after a page/language change; Play preview explicitly resumes it. Native dynamic previews retain title/type/gradient through a transparent, nonactivating sibling layer. Tests use real hidden HWNDs and synthetic frames: same-parent stacking, DPI geometry, transparent corners, no input capture, theme/text updates, destruction and 40 native child paints without additional decoration upload pass. This is not a visible-desktop compositor or arbitrary-codec acceptance test.

Scoped reports:

| Actual Qt ratio | Report directory | Scope |
| --- | --- | --- |
| 1.0 | `.local/appearance-coalesced-dpi100/screens` | Chinese/English × dark/light, 16 scroll positions, settings alignment, three-column grids, metadata, modal guards, bounded decoding and preview ownership passed |
| 1.5 | `.local/appearance-coalesced-dpi150/screens` | Same assertions passed |
| 2.0 | `.local/appearance-coalesced-dpi200/screens` | Same assertions passed |

These three runs used `LightBlockUi-779ac3b2923a.dll` (SHA-256 `779ac3b2923aa9aaa24dc21332f0b5615a169dc718a98ba8c08c1d3d30545ddb`) and native EXE SHA-256 `d30c0c261e2b28f84abb0a34de7576a74f4b712362361eb305a6c708354f7a10`. They ran concurrently in separate hidden fixture processes; timing is not an absolute startup benchmark. A later build, `LightBlockUi-95c278fd3b9e.dll` (SHA-256 `95c278fd3b9e04a7ce25c10bb3eb946107dee98ab8accd17fdbb295373b3136`), passed the added same-GIF Play-button check and all scoped settings/library/preview assertions at ratio 2.0 in `.local/appearance-final-layout-dpi200/screens`. That combined run is **not green**: two newly added capture checks (`native_owner_main_toolbar_survives_eraser` and `noneditable_choices`) failed and were returned to the capture owner for investigation. It must not be used as final package acceptance.

Each directory includes `ui-memory.json`: process private bytes, working set, GDI/USER objects and widget count before fixtures, after fixtures and across 10 page/language/theme rebuild cycles. In the 2.0 run, the 10 cycles retained 413 widgets and 48 GDI objects; private allocation declined from approximately 132 MiB in cycle 1 to 126 MiB in cycle 10. The completed fixture workload holds temporary synthetic images and allocator caches, so these are not steady-state desktop or total media-worker memory claims. No working-set trimming was used.

The remote-catalog, generic-file-icon and missing-GIF-decoration entries in the historical list below have implementation fixes and scoped evidence above. Live media/network acceptance and real system takeover limitations remain open. No formal Python install, user profile, release feed or visible review window was changed by these UI fixtures.

## Final integrated UI check (2026-11-14)

After the native toolbar owner fix and recording ABI 3 audio controls were integrated, all combined UI assertions passed at actual device ratios **1.0 / 1.5 / 2.0**:

- `.local/appearance-abi3-final-dpi100/screens/ui-checks.json`
- `.local/appearance-abi3-final-dpi150/screens/ui-checks.json`
- `.local/appearance-abi3-final-dpi200/screens/ui-checks.json`

Each includes four language/theme variants, scrolling, the new three-column/metadata checks, coalesced decoding, Apply-before-preview-stop ordering, native preview decorations, modal lifetime tests and tool-page regressions. The earlier capture owner/choice failures are resolved: the final capture report passes 16 real hidden/cloaked HWND cycles and 24 Qt reselect/eraser cycles. Recording checks cover the default-off system/microphone choices, start/cancel, recording/merge locks, and 12 crop-toolbar reselect/apply/cancel cycles. No microphone or system sound was captured by these UI fixtures; their controls use fake callbacks. Hardware recording and audio quality belong to the backend acceptance reports.

Exact artifacts:

- UI DLL `LightBlockUi-ec6866f1027f.dll`: SHA-256 `ec6866f1027fd01c1cc1474522e8d1be25f7d028d8c823b9328337e077fcd665`
- Native EXE: SHA-256 `ba0e878102c86c2615c5cb7b201e473deb9e3b360eb8fe9e100215dab6a412b5`
- Packaged recording FFmpeg: SHA-256 `b41556f0e5d8c38dd61ca5a5059c336dfb53158089f5578844ed84ba168e63b1`

The final ratio-2.0 memory report retained 413 widgets and 48 GDI objects across 10 rebuild cycles; private bytes ranged from approximately 127.3 to 138.6 MiB across those cycles after the fixture workload. This confirms no linear object-growth pattern in that sample, not a general leak-proof or low-memory guarantee. Three isolated runs executed concurrently, so timing is not a standalone startup benchmark. Viewed renders show aligned settings, three wallpaper columns, readable recording choices, and intact preview decorations. No claim of pixel-perfect equivalence, visible compositor acceptance, complete hardware coverage, installer acceptance or publication follows from these reports.

## Embedded media centering follow-up (2026-11-14)

A visible review found a 16:9 video occupying the left of a wide hero with unused space only on the right. The Qt host itself was the correct full size: a hidden reproduction measured a 1110 × 384 physical-pixel host and an equally sized MF child before the fix. The embedded MF video worker now occupies the centered contain rectangle, and its renderer targets the complete worker client. It therefore does not depend on the windowed renderer honoring a nonzero destination offset. Desktop wallpaper placement is unchanged. GIF workers keep the full host and use their existing centered image drawing; Qt FFmpeg fallback retains KeepAspectRatio. Motion posters now also use centered contain instead of a cropped fill.

The independent worker SHA-256 was `1b9ba9a46e1c15a68bfd796bb1b77d6d0d09f97b05e8a593b088217d63165e76`. At Qt ratios 1.0, 1.5 and 2.0, MF video, Qt FFmpeg video and GIF each passed three real child-window measurements (wide → tall → wide), decoded frames, reported ready and exited normally. Reports are `.local/preview-centered-dpi{100,150,200}-{mf,gif,qt}/geometry.json`. At ratio 1.5 the 1110 × 384 host contains a video child at x = 213 with width 683: opposing margins differ by one rounding pixel. Resize synchronization still uses the existing periodic check, not a claim of frame-synchronous resizing.

The corresponding `-overlay/preview-native-overlay-checks.json` reports pass the synthetic poster-centering, native-host-size and decoration checks. The first fixture attempt inspected a still-hidden child's deferred geometry and attempted GIF readiness without visible painting; the corrected fixture shows the child before measuring and explicitly uses the existing GIF hidden-probe mode. The actual top-level host never becomes visible. The tests did not capture compositor pixels, use online media or change the user's desktop, and the OS monitor itself stayed at 144 DPI while Qt scale factors varied. Visible playback acceptance and cross-monitor transitions remain separate.

## Earlier local audit

Updated 2026-11-13 after the Qt tool-page migration. The adjacent Python project remains the reference and is unchanged. This ledger distinguishes implementation from verification; it does not claim pixel-perfect or complete behavioral equivalence.

## Verified build

Joint report: `.local/test-20261113-215830/summary.json`. Native executable SHA-256: `310f00d7dcd3b9a2e6f1b65a7c11fc59c6c02d1352e3438616011f3a9a05adf1`. UI DLL: `LightBlockUi-37a2b5d8e728.dll`.

Native checks passed **68/68**, and the combined Qt report has `passed: true`. The main-window renders cover 1250 × 850 and 16 scroll positions. The run was local only: no installation, real desktop takeover or original profile access.

## Resolved initial findings

| Surface | Current implementation | Evidence and limits |
| --- | --- | --- |
| Settings | Embedded Qt pages, single-field immediate commits, unrelated state retained and failed save rolled back; no global Save and apply button. | `ui/ui-checks.json` and native setting regression. Scroll renders do not prove absence of compositor flicker. |
| Local preview | Async static decode and embedded GIF/video; hidden local selection no longer stops an online-owned preview. Suspension restores the play action. | Combined ownership/lifecycle assertions and actual backend playback below. |
| Online library | Original-style virtualized cards, three type filters, bilingual/tag search, attribution, downloaded badges and progress. Preview replaces the left grid in place; the inspector remains. Stop returns to the grid. Explicit intent rejects stale A→B→A completion. | `ui/online-ui-checks.json`: all assertions pass, with zero network requests. Its motion-host fixture does not test real online download or codecs. |
| File workbench | Qt adjustable split view; single-click image/text/folder preview, metadata, Open/Reveal, double-click open/enter, 150-item pages, async SHA-256 and inline rename plans. Text preview is limited to 512 KiB with a notice; folders show up to 120 children. Late/cancelled results are ignored. | `ui/file-workbench-ui-checks.json` and native service regression cover preview, cancellation, plan invalidation and source preservation. No real associated app is launched. |
| Launcher | Frameless Qt palette, app icons, token matching, prefix ranking, 16-result cap, calculator and record/media/files/focus commands. Arrow keys wrap; activation/Escape hides and stops polling. | `ui/launcher-ui-checks.json` and `ui/launcher-service-checks.json`. Activation/copy uses fake callbacks. |
| Clipboard | Qt rows with timestamp, labeled opt-in, Record current content, inline image/text preview, search/copy/pin/delete/clear. Bounded async image decode; hidden pages release preview and stop polling. | `ui/clipboard-ui-checks.json`: 17 checks pass, including visible/hidden/resumed theme changes; zero real clipboard calls. Store retention is tested separately. |
| Focus | Qt centered clock and spin controls, configured idle duration, workspace descriptions and persistent completed rounds/minutes. A session ends once before rest. | Native history/workspace persistence check and `ui/focus-ui-checks.json`; no real wallpaper/layout activation. |
| Notes | Shared desktop/manager text, immediate save, search, completion/archive/reminders, colors, eight resize edges and long-content scrolling without a visible scrollbar. | `ui/notes-ui-checks.json`, including save-failure retention and UTF-8 limits. Manager fixtures create no desktop windows. |
| Capture/recording | Compact dark vector toolbars, grouped parameters and noneditable choices with chevrons. Inline text style/wheel updates keep the same editor and caret. Changing wheel target first commits text; tool groups retain settings. Recording has preview, trim handles, frame keyboard steps and MP4/GIF/lossless MKV. | Native editor regression, `ui/capture-ui-checks.json` and `ui/recording-ui-checks.json`. Actual IME and live recording interaction still need acceptance. |

The default tool pages now use Qt. Retained Win32 fallbacks are not claimed to match the Python UI. The clipboard inspector is an improvement beyond the original history list, not restoration of a feature it already had.

## Actual playback evidence

The bundled JPEG, GIF and MP4 all reported ready with error 0 through the native backend. The GIF advanced 90 frames; the MP4 rendered 83 and reported 2 dropped frames during an approximately four-second sample. This is not an arbitrary-codec or zero-drop guarantee.

Mixed switching confirmed 12 changes with 0 failures, at most 2 children and 0 remaining children. Independent channels and parent-exit cleanup passed. The largest observed heartbeat gap was 172 ms, first-frame delay 875 ms, and the real desktop wallpaper remained unchanged. Timings describe this machine and sample only.

## Status of the earlier differences

The following were findings in the November 13 baseline, not current implementation gaps:

| Historical finding | Preview branch status |
| --- | --- |
| Refresh cleared covers without retrieving a remote catalog | Implemented validated remote refresh with old-catalog retention on failure and transfer guards. Live network acceptance is still separate. |
| Native GIF/video child covered the hero title, type and gradient | Implemented a transparent native sibling decoration layer. Hidden HWND stacking, cache, resize, language/theme and lifecycle checks pass; this is not visible compositor acceptance. |
| File workbench used only generic file/folder icons | Implemented visible-row, bounded asynchronous Shell icons and verified copied pixel ownership. Real network paths and arbitrary Shell extensions remain practical acceptance items. |

## Still unverified behavior

1. **Live online video remains unaccepted.** After the earlier joint fixtures passed, an actual preview of “纽约流光” failed while covers loaded normally. The user confirmed a local network block. Network diagnosis was stopped; no local block, HTTPS policy or digest check was bypassed. Live network preview still needs acceptance in an allowed environment. Successful download/cache hits replace stale failure status; failures retain their HRESULT in the native log.
2. **Real system interaction is not fully covered.** Mixed DPI/multiple monitors, IME, fullscreen transitions, Explorer restart, taskbar/organizer takeover/recovery, long sessions, actual screen recording and scrolling dynamic pages need practical checks. Individual scripted fixes do not establish that all combinations have been accepted.
3. **Failure injection is incomplete.** Focus start is transactional; stop/tick persistence failures are not exhaustively tested. Clipboard copy/pinning and OS contention need live tests; fixtures intentionally avoid changing the user's clipboard.

## Evidence interpretation

Feature JSON reports and rendered images are under `.local/test-20261113-215830/ui/`. Supplemental evidence includes `.local/clipboard-ui-qa/` dark-Chinese/light-English renders and `.local/online-ui-qa/online-live-grid.png`, which loaded nine public covers. Cover success does not establish full-media download success.

Reviewed renders showed no clipped controls at captured sizes. Partial last rows in scroll viewports are normal. A screenshot proves layout output, not every animation frame or input sequence. Keep open differences explicit until corresponding evidence exists.


## Event-driven embedded preview resize (2026-11-14)

The parent reviewer observed that the centered native video was correct after a resize but retained its old rectangle for up to the 500 ms fallback timer. `Preview` now coalesces host resize/move/show events and posts geometry-only control 6 to foreign child windows after Qt commits the native client rectangle. Both native Media Foundation and the Qt media fallback implement that control without restarting playback; GIF still occupies the full host and centers its source within it. The 500 ms timer remains recovery fallback, not the primary response.

Owned hidden-host tests in `.local/preview-event-dpi{100,150,200}-{mf,gif,qt}/geometry.json` passed all nine runs. Each starts a real media subprocess, verifies readiness/frame progression and three physical layouts (wide → tall → wide), checks two event-driven resize responses within 150 ms, then closes only its own worker. The Qt 200% run measured 20/19 ms. Actual Qt ratios were 1.0/1.5/2.0; these environment-controlled ratios do not claim mixed-monitor hot-plug acceptance. The first Qt run failed before readiness with module-not-found because this isolated harness staged its media DLL in the wrong subdirectory; correcting only fixture staging produced the passing results. Product network/codec policies were unchanged.

The parent separately inspected a visible candidate and confirmed initial/final centered composition. The nine hidden runs verify actual worker geometry and playback, not captured compositor pixels or a guarantee of every resize frame on all GPUs. The ordinary annotation visual comparison and full screenshot behavior ledger are now maintained in [CAPTURE_PARITY_MATRIX.md](CAPTURE_PARITY_MATRIX.md); recording has its separate matrix.

## Capture parity and GDI-font validation (2026-11-14)

The scoped full-UI runs `.local/appearance-capture-parity-gdi-dpi{100,150,200}/screens` passed with UI DLL `LightBlockUi-6e7eaa4e2336.dll` and native SHA-256 `5342fd896a2e84c0a689bd5520350a3bfdf34aec316ef2abe906c434602759bd`. The runtime used its default `windows:fontengine=gdi`, without an external font-engine override. Reports cover Chinese/English, dark/light, three-column layout, all tool pages, Capture ABI 3 image-crop controls (Enter/Escape), numbered marks through 9999, underline, and Recording ABI 4. Viewed Chinese and English settings, capture text/crop, and recording renders showed no missing glyphs or clipped action controls. This was a hidden fixture workload, not user-desktop acceptance.

At ratio 2.0, the initial UI sample used about 46.2 MiB private and 82.4 MiB working-set memory. The functional fixture peak includes thousands of temporary test widgets; the following page/theme/language cycles return to 413 widgets and 47 GDI objects. Do not compare these all-tools test peaks with an idle tray process or include only one process when discussing active wallpaper playback. No working-set trimming was used.

A later change migrates the long-capture control card to Qt while retaining native capture/matching/worker state. Its independent reports `.local/scroll-parity/ui-dpi{100,150,200}/scroll-ui-checks.json` pass exact 187-pixel overlap dispatch, noneditable complete overlap choices, borrowed bitmap ownership, bounded preview growth, bilingual card bounds, and 48 unchanged updates with no bitmap copy, window resize, show or hide. Native `ui_bridge_tests` verifies the corresponding manual-stitch/Undo/Cancel state transitions with hidden windows and synthetic pixels. These are separate evidence until the new combined DLL report is recorded; the preceding 6e7eaa build does not contain the Qt long-capture card.
