# Branch status

Updated 2026-11-03.

## Active work to retain

- `main` — integration branch; contains the v2.16 Dr. Mario framebuffer fixes and the PS3 XMB audio metadata validation.
- `feature/xmb-snd0-preview` — retained as the XMB SND0 packaging work branch. Its packaging change is now applied to `main`.
- `fix/dr-mario64-v2.15` — retained as the Dr. Mario fix development lineage.
- `test/dr-mario64-v2.16-pkg` — retained as the tested v2.16 Dr. Mario integration reference.

## Obsolete branches — do not merge or revive

The following branches are superseded, excluded, or unrelated to the selected work:

- `backup/cruisn-fixes-pre-v2.11`
- `ci/narrow-transfer-pak-workflow-filters`
- `duckstation-ps3-port`
- `feat/rar-7z-rom-archives`
- `feature/rumble-pak-support`
- `feature/transfer-pak-support`
- `feature/xmb-snd0-audio-only`
- `feature/zip-rom-support-zlib`
- `fix/cruisn-usa-exotica`
- `fix/cruisn-v2.11-port`
- `fix/dr-mario64-current-main`
- `fix/dr-mario64-invisible-pills`
- `fix/dr-mario64-invisible-pills-main`
- `fix/dr-mario64-rdram-color-copy`
- `fix/dr-mario64-v2.14`
- `fix/xmb-snd0-fact-samples`
- `investigate/dk64-black-screen-intro`
- `licensing/official-7zip-sdk`
- `patch-1`

Do not merge these branches into `main`. Their selected useful work is already represented in `main`, superseded by the retained Dr. Mario integration, or explicitly excluded.
