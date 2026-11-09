# Fritchii v5.5 release alignment

Static comparison performed on 2026-11-10. This identifies the supplied binaries and their
source/build differences; it does not measure frame rates or diagnose a particular runtime crash.

## Inputs and identity

- Supplied upstream executable:
  `C:\Users\admin\Downloads\Fritchii-v5.5-Windows-x64\Fritchii-v5.5\lichii.exe`.
  Size: 49,446,912 bytes. SHA-256:
  `59c6094a6a8ad56be5e9f4c4b3e8d9e24402f473cbfa882f01010b530a3893e3`.
- Previous local executable: `build-gui-release/bin/lichii.exe`.
  Size: 43,302,912 bytes. SHA-256:
  `5355e8469a89ca25f9a7ba5d7c6a6d674be84deb6f3e00f38072c8278565c3e6`.
- Upstream symbols: the `fritchii-v5.5` release's
  `Fritchii-v5.5-debug-symbols-not-needed-to-play.zip`, SHA-256:
  `0fb007c39566e284c216104561d2f257516110e0e3a19138d395d82eb1dd3c3d`.

The upstream EXE embeds commit `4012e5271e278b21cf8f56b97481dac2b777cd0d`, branch
`oot-gather-fix`, version `v5.5`, and compiler `Clang 22.1.8`. The Git tag
`fritchii-v5.5` and `upstream/oot-gather-fix` both resolved to that commit at inspection.
The embedded build timestamp is `2026-11-09T13:04:26Z`.

The previous local EXE embeds `2910a84fc020227f23f8c152bf40c23418c84427`, branch `nx2`,
compiler `MSVC 19.44.35229.0`, and build timestamp `2026-11-09T08:57:32Z`.
Its base was `upstream/nx2` at `f66ea633db`, with five local commits. The default upstream
branch was not the branch used for the supplied release.

REA's static PE resource parser confirmed the upstream executable's hash, x64 PE format,
resource/icon tree, and absence of a version-information resource. Identity therefore comes
from embedded SCM strings corroborated by Git, rather than Windows FileVersion.
Both programs import the same set of DLL names; matching imports alone do not establish
matching implementations. The supplied Qt6Core DLL is 6.8.3.0; the old local DLL is 6.9.3.0.
The downloaded Clang build's Qt6Core, Qt6Gui, Qt6Widgets and Qt6Charts DLLs were also
compared by SHA-256 and match the supplied release's copies exactly. The release PDB's
GUID `{5162F7F5-12A2-E63C-4C4C-44205044422E}` and age 1 match its EXE CodeView record.

## Source differences that explain the mismatch

`git diff --shortstat upstream/nx2 fritchii-v5.5` reported 553 files, 79,102 inserted lines,
and 2,327 deleted lines. The branch histories diverge: comparing the previous local HEAD
to v5.5 yields 6 local-only and 816 release-only reachable commits, including merges.
These are not 816 independent missing fixes.

- UI: 56 changed files under `src/yuzu`. Commit `908995e2f4` adds Graphite and the Fritchii
  home screen; `c1770d75fe` adds the pre-game shader preparation screen. Branding and version
  selection are controlled by `dist/frizti_version.txt` and `GenerateSCMRev.cmake`.
  `src/yuzu/fritchii/home.cpp`, the in-game menus, settings scrolling, and export-log features
  are part of the release branch. This is more than a Qt theme preference.
- GPU/runtime: 191 changed files under `src/video_core`, 51 in `src/shader_recompiler`,
  and 47 in `src/core`. Changes cover memory, native shader translation, cached pipeline
  preparation, frame timing, game-specific compatibility, and task/mesh fallback.
- Precompilation: the supplied package includes `recipes/OoT v131072 (cache v54).nx2recipe`
  (1,821,351 bytes). v5.5 includes its preparation/import path. The older nx2-based package
  did not supply that release workflow or recipe.
- Defaults: `src/common/frizti_settings.{h,cpp}` uses revision 5 and reconciles tuned global
  settings and OoT overrides. EDS2, dynamic vertex input, asynchronous shaders, mailbox
  presentation, memory layout, resolution scale, and filters can therefore differ even
  when the same user configuration directory is used.
- Frame generation: `d64f34764a` prevents it from running in Fritchii, including when an old
  configuration left it enabled. Upstream identified camera flicker and disabled frame
  pacing from that old state. This explains a possible difference; it does not prove the
  user's particular symptom was caused by that setting.
- Further examples: `4012e5271e` configures the process's NVIDIA shader cache;
  `35cb046326` makes DLSS global and adds the preset selector; `9ea66c8273` adapts precompiled
  logic-op state for AMD; `f52631cf66` adds ASTC/unswizzle store bounds checks; `464fbb45bb`
  permits host buffers over the budget instead of aborting.

The primary established cause is the different source branch. The compiler/profile/default
differences provide additional plausible performance causes. Their individual contributions
and any exact frame-rate equivalence require controlled runtime comparison.

## Upstream build evidence and limits

The EXE's CodeView record names `E:\lichii-src\build\pgo-release\bin\lichii.pdb`.
The matching PDB contains Clang 22.1.8 compile commands with `-O3`, a generic x86-64 target
and SSE4.1/CX16, `NDEBUG`, and:

```
-fprofile-instrument-use=clang
-fprofile-instrument-use-path=E:/OotRemakeEmulation/pgo/lichii-v3.profdata
```

LTO-generated object names are present, and the recorded lld-link command includes
`/DEBUG /INCREMENTAL:NO /OPT:REF,NOICF /opt:lldltojobs=12`.
The compile environment references MSVC 14.50 headers and Windows SDK 10.0.26100.0.

The author's `lichii-v3.profdata` is not present in the release source tree or its two
release assets. The repository's `eglantine.profdata` is another profile and LLVM 22.1.8
rejects its instrumentation format. Do not substitute it silently or claim that it is
the profile used for v5.5.

The reproducible local build uses the exact v5.5 source baseline, Clang 22.1.8, lld-link,
Qt 6.8.3, `-O3`, ThinLTO, and 12 compile jobs. It uses the available MSVC 14.44 headers
and Windows SDK. PGO is disabled unless a compatible profile is explicitly supplied.
Consequently this is a release-source-aligned build, not a bit-identical or performance-
identical reproduction of the author's build.

## FSR 4.1.2 port

Branch `codex/fritchii-v5.5-fsr4.1.2` starts at the release tag and ports only the prior
FSR integration and necessary build support. The conflict resolution keeps the release's
global DLSS setting, preset selector, home screen, game fixes, and OoT-specific DLSS path.
The new FSR setting is global too. FSR is off by default, and AMD DLLs are loaded only when
the supported temporal path requests them.

The existing validated FSR contract remains Fire Emblem: Fortune's Weave only. The upstream
OoT DLSS path synthesizes its own camera-motion inputs through a separate implementation;
this port does not claim or add FSR 4 support for OoT. The RTX 4060 Ti's previous provider
query did not expose FSR 4.1.2; integration does not change that hardware limitation.

Build from the repository root:

```powershell
./tools/windows/build-fritchii-v55-fsr4.ps1
# If the author supplies the actual compatible training profile:
./tools/windows/build-fritchii-v55-fsr4.ps1 -PgoProfile 'E:/path/lichii-v3.profdata'
```

Toolchain directories and the build directory can be overridden using the script parameters.
The checked-in build script validates an explicitly supplied profile before use. Build
artifacts and raw comparison evidence stay in ignored `build-*` directories.

## Local verification on 2026-11-10

- Fresh Clang 22.1.8 / Qt 6.8.3 Release GUI and test executables built successfully with
  `-O3`, ThinLTO, 12 jobs and no PGO. Ninja records 318 dependencies for `settings.cpp.obj`,
  including headers; kernel and fiber LTO exclusions remain in effect.
- The upstream test file `pipeline_cache_files.cpp` contained two identical `Header(u32)`
  definitions. One duplicate was removed to compile the tests. No assertions were changed.
- Required FSR UI includes (`QTimer` and `game_dlss_status.h`) were restored after porting
  to the release branch, which had removed the older status-panel includes.
- FSR, DLSS contracts, FE/Star Fox selection, OoT network and pipeline-cache file tests:
  **26 cases and 932 assertions passed**. Including `[frizti_settings]` gives 31/32 cases
  and 994/995 assertions passing: its unchanged fixture modifies only 10 settings but
  expects changes for all 17 tuned settings (`frizti_settings.cpp:51`).
- The complete suite is **not passing or fully validated**. It exits with Windows stack
  overflow `0xC00000FD` using the default 1 MiB test stack. A diagnostic copy of the test
  executable, with only its stack reserve raised to 32 MiB using `editbin`, proceeds further
  but exits with access violation `0xC0000005`. Neither experiment changes the GUI executable
  or emulator logic. Failures reported before that exit include global-memory DontInline
  checks (`global_memory_coherence.cpp:430`), video scaler comparisons
  (`nx2_video_render.cpp:151`, `:210`), NUP metadata (`nup_writer.cpp:671`), and the settings
  fixture above. These are outside the FSR integration changes; their runtime causes are
  not established by this static comparison.
- REA's resource inspection confirms that the new EXE has the same ten Windows resources
  and matching icon payloads as the supplied v5.5 EXE. Fritchii home/theme and the OoT path
  source files remain identical to the release tag.
- No game frame-rate, long-session stability, or AMD FSR 4 image-quality validation was
  performed for this build. Those claims cannot be inferred from the static comparison
  or the targeted unit tests.
