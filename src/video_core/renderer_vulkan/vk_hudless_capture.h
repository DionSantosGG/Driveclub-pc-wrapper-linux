// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <map>
#include <memory>
#include <vector>
#include "video_core/texture_cache/image.h"

namespace Vulkan {
class Instance;
class Runtime;
class Scheduler;

struct HudlessSnapshot {
    VideoCore::UniqueImage native, prepared;
    vk::UniqueImageView native_view, prepared_view;
    vk::Extent2D native_size{}, prepared_size{};
    vk::Format native_format{}, view_format{}, prepared_format{};
};

// driveclub-pc: snapshot the real video-out scene before its HUD draws, once per guest flip.
class HudlessCapture {
public:
    explicit HudlessCapture(const Instance& instance) : instance{instance} {}
    static bool Enabled();
    void Register(VAddr address);
    void OnDraw(Scheduler& scheduler, Runtime& runtime, VideoCore::Image& image, VAddr address);
    std::shared_ptr<HudlessSnapshot> Take(VAddr address);

private:
    struct Surface {
        u32 draws{};
        std::shared_ptr<HudlessSnapshot> pending;
    };
    const Instance& instance;
    std::map<VAddr, Surface> surfaces;
    std::vector<std::shared_ptr<HudlessSnapshot>> pool;
    u64 captures{}, missing{};
};
} // namespace Vulkan
