// SPDX-FileCopyrightText: Copyright 2026 Killzone port contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <chrono>
#include "imgui/imgui_texture.h"

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/texture_cache/image.h"

struct FfxVkPortableFrameGenerationContext;

namespace Vulkan {

class Instance;

/// Sleeps until `due` within ~0.1 ms (high-resolution waitable timer, then a short spin).
void PreciseSleepUntil(std::chrono::steady_clock::time_point due);

/// kz: FSR 3 frame interpolation on the presented frame (native Vulkan, FireBurn/FSR-Vulkan).
/// The game gives no motion vectors or depth: constant inputs (far depth, zero motion) leave
/// the interpolation to FSR's optical flow. Presenter thread only.
class FrameGenerator {
public:
    static bool Enabled();
    explicit FrameGenerator(const Instance& instance);
    ~FrameGenerator();

    /// Records the frame halfway between the previous call's frame and `src` (w x h, General
    /// layout, transfer-src usage) into Output() (General layout afterwards). False when there
    /// is nothing to show yet: first frame, size change, reset or failure.
    bool Record(vk::CommandBuffer cmdbuf, vk::Image src, u32 w, u32 h, float frame_ms, bool reset,
                vk::Image hudless_src = {});

    [[nodiscard]] vk::Image Output() const {
        return vk::Image(output);
    }

    /// Frames handed out and failures; GPU time of the copy + optical flow + interpolation
    /// (timestamp queries, read back one frame later) summed since the caller reset it.
    ImTextureID Texture() const {
        return output_texture;
    }

    u64 generated = 0, failures = 0;
    double gpu_ms = 0;
    u64 gpu_samples = 0;

private:
    bool Create(u32 w, u32 h);
    void Destroy();

    const Instance& instance;
    FfxVkPortableFrameGenerationContext* context = nullptr;
    VideoCore::UniqueImage source, output, depth, motion, hudless;
    bool last_hudless = false;
    vk::UniqueImageView output_view;
    ImTextureID output_texture{};
    u32 width = 0, height = 0;
    u64 frame_id = 0;
    bool inputs_cleared = false;
    bool have_previous = false;
    bool broken = false; ///< creation failed: stays off until the size changes
    vk::QueryPool queries{};
    u32 query_slot = 0; ///< pair written this frame (two pairs alternate)
    bool query_written[2]{};
};

} // namespace Vulkan
