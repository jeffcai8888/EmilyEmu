// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <limits>
#include <numbers>
#include <utility>
#include <vector>

#include "common/dynamic_library.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "video_core/fsr4_policy.h"
#include "video_core/game_dlss_status.h"
#include "video_core/renderer_vulkan/vk_fsr4_upscaler.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"

#define FFX_API_ENTRY
#include <api/include/vk/ffx_api_vk.h>
#include <upscalers/include/ffx_upscale.h>

#include <softpub.h>
#include <wintrust.h>

namespace Vulkan {
namespace {

constexpr VkFormat OUTPUT_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkImageSubresourceRange COLOR_RANGE{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

bool SignedRuntime(const std::filesystem::path& path) {
    WINTRUST_FILE_INFO file{};
    file.cbStruct = sizeof(file);
    file.pcwszFilePath = path.c_str();
    WINTRUST_DATA trust{};
    trust.cbStruct = sizeof(trust);
    trust.dwUIChoice = WTD_UI_NONE;
    trust.fdwRevocationChecks = WTD_REVOKE_NONE;
    trust.dwUnionChoice = WTD_CHOICE_FILE;
    trust.pFile = &file;
    trust.dwStateAction = WTD_STATEACTION_VERIFY;
    trust.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG result = WinVerifyTrust(nullptr, &action, &trust);
    trust.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &action, &trust);
    return result == ERROR_SUCCESS;
}

FfxApiResource Resource(const Fsr4Upscaler::Image& image, u32 usage, u32 state) {
    FfxApiResourceDescription desc{};
    desc.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
    desc.format = ffxApiGetSurfaceFormatVK(image.format);
    desc.width = image.extent.width;
    desc.height = image.extent.height;
    desc.depth = 1;
    desc.mipCount = 1;
    desc.usage = usage;
    return ffxApiGetResourceVK(reinterpret_cast<void*>(image.image), desc, state);
}

} // Anonymous namespace

class Fsr4Upscaler::Impl {
public:
    Impl(const Device& device_, Scheduler& scheduler_, MemoryAllocator& allocator_)
        : device{device_}, scheduler{scheduler_}, allocator{allocator_} {
        VideoCore::SetFsr4State(VideoCore::Fsr4State::Waiting);
        const auto directory = Common::FS::GetExeDirectory() / "fsr4";
        const auto loader = directory / "amd_fidelityfx_loader_vk.dll";
        const auto effect = directory / "amd_fidelityfx_upscaler_vk.dll";
        if (!SignedRuntime(loader) || !SignedRuntime(effect)) {
            Unavailable("the signed Vulkan runtime is missing or its signature is invalid");
            return;
        }
        constexpr DWORD search = LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS;
        effect_module = Common::DynamicLibrary{LoadLibraryExW(effect.c_str(), nullptr, search)};
        api_module = Common::DynamicLibrary{LoadLibraryExW(loader.c_str(), nullptr, search)};
        if (!effect_module.IsOpen() || !api_module.IsOpen() ||
            !api_module.GetSymbol("ffxCreateContext", &create) ||
            !api_module.GetSymbol("ffxDestroyContext", &destroy) ||
            !api_module.GetSymbol("ffxQuery", &query) ||
            !api_module.GetSymbol("ffxDispatch", &dispatch)) {
            Unavailable("the Vulkan runtime could not be loaded");
            return;
        }
        backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
        backend.deviceContext = {*device.GetLogical(), static_cast<VkPhysicalDevice>(device.GetPhysical()),
                                  device.GetDispatchLoader().vkGetDeviceProcAddr};
        u64 count{};
        ffxQueryDescGetVersions versions{};
        versions.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
        versions.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
        versions.device = &backend.deviceContext;
        versions.outputCount = &count;
        if (query(nullptr, &versions.header) != FFX_API_RETURN_OK || count == 0 || count > 64) {
            Unavailable("AMD reported no compatible upscaling provider");
            return;
        }
        const auto capacity = count;
        std::vector<u64> ids(static_cast<size_t>(capacity));
        std::vector<const char*> names(static_cast<size_t>(capacity));
        versions.versionIds = ids.data();
        versions.versionNames = names.data();
        if (query(nullptr, &versions.header) != FFX_API_RETURN_OK || count > capacity) {
            Unavailable("the provider version query failed");
            return;
        }
        for (size_t i = 0; i < static_cast<size_t>(count); ++i) {
            if (names[i] && VideoCommon::Fsr4::IsVersion412(names[i])) {
                version.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
                version.header.pNext = &backend.header;
                version.versionId = ids[i];
                available = true;
                LOG_INFO(Render_Vulkan, "AMD FSR 4.1.2 Vulkan provider available: {}", names[i]);
                return;
            }
        }
        Unavailable("FSR 4.1.2 is not available on this GPU and driver");
    }

    ~Impl() {
        if (context) {
            // The rasterizer already drained recording; its query cache may be gone by now.
            void(device.GetLogical().WaitIdle());
            destroy(&context, nullptr);
        }
        VideoCore::SetFsr4State(VideoCore::Fsr4State::Waiting);
    }

    bool Upscale(const Inputs& in) {
        if (!available || failed.load(std::memory_order_relaxed)) {
            return false;
        }
        if (!EnsureContext(in)) {
            return false;
        }
        ffxDispatchDescUpscale evaluation{};
        evaluation.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
        evaluation.color = Resource({*input_copies[0], in.color.format, in.color.extent},
                                     FFX_API_RESOURCE_USAGE_READ_ONLY, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        evaluation.depth = Resource({*input_copies[1], in.depth.format, in.depth.extent},
                                     FFX_API_RESOURCE_USAGE_DEPTHTARGET, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        evaluation.motionVectors = Resource({*input_copies[2], in.motion_vectors.format, in.motion_vectors.extent},
                                             FFX_API_RESOURCE_USAGE_READ_ONLY, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        evaluation.output = Resource({*output, OUTPUT_FORMAT, upscale_extent},
                                     FFX_API_RESOURCE_USAGE_UAV, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        evaluation.jitterOffset = {in.jitter[0], in.jitter[1]};
        evaluation.motionVectorScale = {in.motion_vector_scale[0], in.motion_vector_scale[1]};
        evaluation.renderSize = {in.render_size.width, in.render_size.height};
        evaluation.upscaleSize = {in.upscale_size.width, in.upscale_size.height};
        evaluation.enableSharpening = in.sharpen;
        evaluation.sharpness = std::clamp(in.sharpness, 0.0f, 1.0f);
        evaluation.frameTimeDelta = std::clamp(in.frame_time_ms, 1.0f, 1000.0f);
        evaluation.preExposure = 1.0f;
        evaluation.reset = std::exchange(first_frame, false) || in.reset;
        evaluation.cameraNear = 0.1f;
        evaluation.cameraFar = std::numeric_limits<f32>::max();
        evaluation.cameraFovAngleVertical = std::numbers::pi_v<f32> / 3.0f;
        evaluation.viewSpaceToMetersFactor = 1.0f;

        scheduler.RequestOutsideRenderPassOperationContext();
        scheduler.Record([this, in, evaluation](vk::CommandBuffer cmdbuf) {
            auto parameters = evaluation;
            const VkMemoryBarrier before{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                                          VK_ACCESS_MEMORY_WRITE_BIT,
                                          VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
            cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, before);
            if (!failed.load(std::memory_order_relaxed)) {
                CopyInputs(cmdbuf, in);
            }
            parameters.commandList = reinterpret_cast<void*>(*cmdbuf);
            const auto result = failed.load(std::memory_order_relaxed)
                                    ? FFX_API_RETURN_ERROR
                                    : dispatch(&context, &parameters.header);
            if (result != FFX_API_RETURN_OK) {
                if (!failed.exchange(true)) {
                    LOG_ERROR(Render_Vulkan, "AMD FSR 4.1.2 dispatch failed ({}); using FSR 3.1 "
                                              "from the next frame", result);
                }
                VideoCore::SetFsr4State(VideoCore::Fsr4State::Unavailable);
                // A failed external dispatch does not promise a final image layout.
                const VkImageMemoryBarrier to_clear{
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
                    .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                    .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                    .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = *output,
                    .subresourceRange = COLOR_RANGE,
                };
                cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, to_clear);
                const VkClearColorValue clear{};
                cmdbuf.ClearColorImage(*output, VK_IMAGE_LAYOUT_GENERAL, clear, COLOR_RANGE);
            } else if (VideoCore::GetFsr4State() != VideoCore::Fsr4State::Active) {
                VideoCore::SetFsr4State(VideoCore::Fsr4State::Active);
                LOG_INFO(Render_Vulkan, "AMD FSR 4.1.2 is recording temporal upscaling dispatches");
            }
            const VkMemoryBarrier after{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                                         VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
                                         VK_ACCESS_SHADER_READ_BIT};
            cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, after);
        });
        scheduler.InvalidateState();
        return true;
    }

    void CopyInputs(vk::CommandBuffer cmdbuf, const Inputs& in) {
        const std::array sources{in.color, in.depth, in.motion_vectors};
        for (size_t i = 0; i < sources.size(); ++i) {
            const auto& source = sources[i];
            const VkImageAspectFlags aspect = i == 1 ? VK_IMAGE_ASPECT_DEPTH_BIT
                                                      : VK_IMAGE_ASPECT_COLOR_BIT;
            const VkImageSubresourceRange range{aspect, 0, 1, 0, 1};
            const VkImageMemoryBarrier source_read{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = source.image,
                .subresourceRange = range,
            };
            const VkImageMemoryBarrier copy_write{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT,
                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = *input_copies[i],
                .subresourceRange = range,
            };
            const std::array before{source_read, copy_write};
            cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT, 0, {}, {}, before);
            const VkImageCopy copy{
                .srcSubresource = {aspect, 0, 0, 1},
                .srcOffset = {},
                .dstSubresource = {aspect, 0, 0, 1},
                .dstOffset = {},
                .extent = {source.extent.width, source.extent.height, 1},
            };
            cmdbuf.CopyImage(source.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             *input_copies[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, copy);
            auto source_restore = source_read;
            source_restore.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            source_restore.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            source_restore.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            source_restore.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            auto copy_read = copy_write;
            copy_read.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            copy_read.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            copy_read.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            copy_read.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            const std::array after{source_restore, copy_read};
            cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, {}, {}, after);
        }
    }

    bool EnsureContext(const Inputs& in) {
        const auto render = in.render_size;
        const auto upscale = in.upscale_size;
        const std::array sources{in.color, in.depth, in.motion_vectors};
        bool same_resources = true;
        for (size_t i = 0; i < sources.size(); ++i) {
            same_resources &= sources[i].extent.width == copy_descriptions[i].extent.width &&
                              sources[i].extent.height == copy_descriptions[i].extent.height &&
                              sources[i].format == copy_descriptions[i].format;
        }
        if (context && render.width == render_extent.width && render.height == render_extent.height &&
            upscale.width == upscale_extent.width && upscale.height == upscale_extent.height &&
            same_resources) {
            return true;
        }
        if (context) {
            scheduler.Finish();
            destroy(&context, nullptr);
            context = nullptr;
        }
        desc = {};
        desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
        api_version.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION;
        api_version.header.pNext = &version.header;
        api_version.version = FFX_UPSCALER_VERSION;
        desc.header.pNext = &api_version.header;
        desc.flags = FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE | FFX_UPSCALE_ENABLE_DEPTH_INVERTED |
                     FFX_UPSCALE_ENABLE_DEPTH_INFINITE | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
        desc.maxRenderSize = {render.width, render.height};
        desc.maxUpscaleSize = {upscale.width, upscale.height};
        const auto result = create(&context, &desc.header, nullptr);
        if (result != FFX_API_RETURN_OK) {
            LOG_WARNING(Render_Vulkan, "AMD FSR 4.1.2 context creation failed ({}); using FSR 3.1",
                        result);
            Unavailable("the provider could not create a Vulkan context");
            return false;
        }
        ffxQueryGetProviderVersion provider{};
        provider.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
        if (query(&context, &provider.header) != FFX_API_RETURN_OK ||
            provider.versionId != version.versionId || !provider.versionName ||
            !VideoCommon::Fsr4::IsVersion412(provider.versionName)) {
            destroy(&context, nullptr);
            context = nullptr;
            Unavailable("the runtime did not honor the requested FSR 4.1.2 provider");
            return false;
        }
        render_extent = render;
        upscale_extent = upscale;
        first_frame = true;
        for (size_t i = 0; i < sources.size(); ++i) {
            const auto& source = sources[i];
            input_copies[i] = allocator.CreateImage(VkImageCreateInfo{
                .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                .imageType = VK_IMAGE_TYPE_2D,
                .format = source.format,
                .extent = {source.extent.width, source.extent.height, 1},
                .mipLevels = 1,
                .arrayLayers = 1,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .tiling = VK_IMAGE_TILING_OPTIMAL,
                .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            });
            copy_descriptions[i] = source;
        }
        output_view = {};
        output = allocator.CreateImage(VkImageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = OUTPUT_FORMAT,
            .extent = {upscale.width, upscale.height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        });
        output_view = device.GetLogical().CreateImageView(VkImageViewCreateInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = *output,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = OUTPUT_FORMAT,
            .subresourceRange = COLOR_RANGE,
        });
        if (!sampler) {
            sampler = device.GetLogical().CreateSampler(VkSamplerCreateInfo{
                .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                .magFilter = VK_FILTER_NEAREST,
                .minFilter = VK_FILTER_NEAREST,
                .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .maxLod = VK_LOD_CLAMP_NONE,
            });
        }
        scheduler.RequestOutsideRenderPassOperationContext();
        scheduler.Record([image = *output](vk::CommandBuffer cmdbuf) {
            const VkImageMemoryBarrier barrier{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = image,
                .subresourceRange = COLOR_RANGE,
            };
            cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, barrier);
        });
        return true;
    }

    void Unavailable(const char* reason) {
        available = false;
        VideoCore::SetFsr4State(VideoCore::Fsr4State::Unavailable);
        LOG_WARNING(Render_Vulkan, "AMD FSR 4.1.2 unavailable: {}; using FSR 3.1", reason);
    }

    Common::DynamicLibrary effect_module;
    Common::DynamicLibrary api_module;
    const Device& device;
    Scheduler& scheduler;
    MemoryAllocator& allocator;
    PfnFfxCreateContext create{};
    PfnFfxDestroyContext destroy{};
    PfnFfxQuery query{};
    PfnFfxDispatch dispatch{};
    ffxContext context{};
    ffxCreateContextDescUpscale desc{};
    ffxOverrideVersion version{};
    ffxCreateContextDescUpscaleVersion api_version{};
    ffxCreateBackendVKDesc backend{};
    VkExtent2D render_extent{};
    VkExtent2D upscale_extent{};
    vk::Image output;
    std::array<vk::Image, 3> input_copies;
    std::array<Image, 3> copy_descriptions{};
    vk::ImageView output_view;
    vk::Sampler sampler;
    std::atomic<bool> failed{};
    bool available{};
    bool first_frame{true};
};

Fsr4Upscaler::Fsr4Upscaler(const Device& device, Scheduler& scheduler, MemoryAllocator& allocator)
    : impl{std::make_unique<Impl>(device, scheduler, allocator)} {}
Fsr4Upscaler::~Fsr4Upscaler() = default;

bool Fsr4Upscaler::Upscale(const Inputs& inputs) {
    return impl->Upscale(inputs);
}
VkImage Fsr4Upscaler::OutputImage() const { return *impl->output; }
VkImageView Fsr4Upscaler::OutputView() const { return *impl->output_view; }
VkSampler Fsr4Upscaler::PointSampler() const { return *impl->sampler; }

} // namespace Vulkan
