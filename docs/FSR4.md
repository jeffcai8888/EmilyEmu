# AMD FSR Upscaling 4.1.2

Windows x64 Vulkan builds support AMD's signed FSR 4.1.2 API from FidelityFX SDK
v2.4.0. This is temporal upscaling, using color, depth, motion vectors and jitter
from the existing validated game-DLSS replacement path. Currently that temporal
contract covers Fire Emblem: Fortune's Weave. Other games and the window adapting
filters do not gain a temporal FSR 4 path automatically.

Enable **Use AMD FSR 4.1.2 in place of a game's DLSS** in Graphics, then start the
game. The status below the setting reports waiting, active or unavailable.
The setting defaults to off. NVIDIA DLSS remains available when FSR 4 is off.

`EDEN_NX2_DLSS_REPLACEMENT=fsr4` explicitly selects this backend. Existing `fsr`,
`dlss` and `native` overrides take precedence over the checkbox. The FSR 4 backend
queries the versions AMD exposes for the Vulkan device, selects the returned ID
whose version is exactly 4.1.2, and verifies the context's provider after creation.
A missing runtime, unsupported GPU/driver or context creation error falls back to
the existing FSR 3.1 implementation. A dispatch recording error clears that
evaluation's output and uses FSR 3.1 on subsequent frames.

## Building and packaging

`ENABLE_AMD_FSR4` defaults to on for Windows x64 and off elsewhere. It adds no AMD
link-time import dependency. `FSR4_BUNDLE_RUNTIME=ON` downloads two pinned,
SHA-256-checked binaries and packages them beside the executable:

```
fsr4/amd_fidelityfx_loader_vk.dll
fsr4/amd_fidelityfx_upscaler_vk.dll
fsr4/AMD-LICENSE.md
```

Both DLLs must retain valid Windows signatures. The loader uses this explicit
directory, and the implementation pins the actual provider to FSR 4.1.2 rather
than accepting an older FSR provider under the same setting name.
`FSR4_BUNDLE_RUNTIME=OFF` permits builds without downloading the runtime; supply
the same signed files in `fsr4/` before enabling the feature.

For a localized MSVC toolchain, verify that Ninja records header dependencies.
`LICHII_MSVC_SHOWINCLUDES_PREFIX` overrides an incorrectly decoded CMake detection.
After correcting a build that had zero recorded dependencies, rebuild cleanly so
all objects use the same settings and structure layouts.

The API headers, provenance and redistribution terms are in `externals/FSR4`.
The proprietary signed binaries remain outside Git. They may be redistributed
with their accompanying notice under AMD's binary redistribution terms.

## Validation limits

Version selection, override precedence and the game contract have regression
tests tagged `[fsr4]`. Build and fallback checks on an NVIDIA GPU do not validate
FSR 4 image quality. Full temporal evaluation and visual validation require a GPU
and driver for which AMD reports an FSR 4.1.2 provider.

The adapter retains the existing FSR 3 depth convention: reversed infinite depth,
an assumed 0.1 near plane and a 60-degree vertical field of view. The physical
near/far API arguments follow AMD's SDK v2.4.0 sample; they are not interchanged.

Local Windows validation on 2026-11-09:

- Clean x64 Release GUI build with MSVC 19.44 and Qt 6.9.3; startup and the settings
  dialog checked using a fresh portable user directory.
- FSR 4, DLSS input contracts and related game selection: 15 cases, 870 assertions passed.
- Full suite: 631 of 634 cases passed. Two video scaler pixel comparisons and one
  NUP metadata rewrite case failed; these results are retained as validation limits.
- AMD's signed runtime loaded successfully. On the RTX 4060 Ti it reported providers
  3.1.6 and 2.3.4, which the FSR 4.1.2 selector rejects instead of reporting them as FSR 4.
- Legacy signed-comparison warnings were downgraded only while compiling the test
  executable (`_CL_=/wd4388 /wd4389`); the GUI build retained its normal diagnostics.
