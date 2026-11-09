# AMD FSR Upscaling 4.1.2

The API headers are from AMD FidelityFX SDK `v2.4.0`, commit
`7539ae8b6568ad29ed78a753404351daf3b5cca7`. Their MIT notices are retained.
`ffx_api.h` has one local change: `FFX_API_ENTRY` can be overridden when loading
the API dynamically instead of exporting or linking its functions.

The Windows x64 Vulkan runtime is downloaded by `CMakeModules/FSR4.cmake` from
that commit, checked against SHA-256, and copied into the executable's `fsr4/`
directory. The proprietary signed binaries are not checked into this repository.
Their redistribution notice, including the headers' license exceptions, is in
`LICENSE.md` and is shipped with the runtime.

Official documentation:
https://gpuopen.com/amd-fsr-upscaling/

SDK:
https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/tree/v2.4.0
