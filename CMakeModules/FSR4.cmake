# SPDX-License-Identifier: GPL-3.0-or-later

cmake_dependent_option(ENABLE_AMD_FSR4 "Enable AMD FSR 4.1.2 through its Vulkan runtime" ON
                       "WIN32 AND ARCHITECTURE_x86_64" OFF)
if (NOT ENABLE_AMD_FSR4)
    return()
endif()

option(FSR4_BUNDLE_RUNTIME "Download and package AMD's signed FSR 4.1.2 Vulkan runtime" ON)
target_sources(video_core PRIVATE
    renderer_vulkan/vk_fsr4_upscaler.cpp
    renderer_vulkan/vk_fsr4_upscaler.h)
target_include_directories(video_core SYSTEM PRIVATE
    "${PROJECT_SOURCE_DIR}/externals/FSR4")
target_compile_definitions(video_core PUBLIC HAS_AMD_FSR4)
target_link_libraries(video_core PRIVATE wintrust crypt32)

if (FSR4_BUNDLE_RUNTIME)
    set(fsr4_cache "${CPM_SOURCE_CACHE}/fsr4/4.1.2")
    set(fsr4_output "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/fsr4")
    set(fsr4_url "https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/7539ae8b6568ad29ed78a753404351daf3b5cca7/Kits/FidelityFX/signedbin")
    file(MAKE_DIRECTORY "${fsr4_cache}")

    function(fsr4_runtime_file name sha256)
        set(cached "${fsr4_cache}/${name}")
        set(actual "")
        if (EXISTS "${cached}")
            file(SHA256 "${cached}" actual)
        endif()
        if (NOT actual STREQUAL sha256)
            file(DOWNLOAD "${fsr4_url}/${name}" "${cached}.download"
                 EXPECTED_HASH "SHA256=${sha256}" TLS_VERIFY ON)
            file(RENAME "${cached}.download" "${cached}")
        endif()
        add_custom_command(OUTPUT "${fsr4_output}/${name}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${fsr4_output}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${cached}" "${fsr4_output}/${name}"
            DEPENDS "${cached}" VERBATIM)
    endfunction()

    fsr4_runtime_file(amd_fidelityfx_loader_vk.dll
        38a56a01fb3565c0757d9f5c1e4e69de4b65e5f2e5769b323b200dc1f142bab9)
    fsr4_runtime_file(amd_fidelityfx_upscaler_vk.dll
        4a7e895af6e0b615d40a84d59eb8c2b1054ad91cdd0c739567940270d51bdab7)
    add_custom_command(OUTPUT "${fsr4_output}/AMD-LICENSE.md"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${fsr4_output}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/externals/FSR4/LICENSE.md" "${fsr4_output}/AMD-LICENSE.md"
        DEPENDS "${PROJECT_SOURCE_DIR}/externals/FSR4/LICENSE.md" VERBATIM)
    add_custom_target(fsr4_runtime DEPENDS
        "${fsr4_output}/amd_fidelityfx_loader_vk.dll"
        "${fsr4_output}/amd_fidelityfx_upscaler_vk.dll"
        "${fsr4_output}/AMD-LICENSE.md")
    add_dependencies(video_core fsr4_runtime)
endif()
