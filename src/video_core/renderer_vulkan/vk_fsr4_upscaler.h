// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <memory>

#include "common/common_types.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

class Device;
class MemoryAllocator;
class Scheduler;

class Fsr4Upscaler {
public:
    struct Image {
        VkImage image;
        VkFormat format;
        VkExtent2D extent;
    };

    struct Inputs {
        Image color;
        Image depth;
        Image motion_vectors;
        VkExtent2D render_size;
        VkExtent2D upscale_size;
        std::array<f32, 2> jitter;
        std::array<f32, 2> motion_vector_scale;
        f32 frame_time_ms;
        bool reset;
        bool sharpen;
        f32 sharpness;
    };

    Fsr4Upscaler(const Device& device, Scheduler& scheduler, MemoryAllocator& allocator);
    ~Fsr4Upscaler();

    // Returns false when the signed runtime, exact provider version or inputs are unavailable.
    // A recording failure disables this backend for subsequent frames.
    bool Upscale(const Inputs& inputs);
    VkImage OutputImage() const;
    VkImageView OutputView() const;
    VkSampler PointSampler() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace Vulkan
