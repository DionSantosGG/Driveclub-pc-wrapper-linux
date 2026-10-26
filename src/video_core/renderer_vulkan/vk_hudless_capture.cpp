// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include <algorithm>
#include <cstdlib>
#include "video_core/renderer_vulkan/vk_frame_gen.h"
#include "video_core/renderer_vulkan/vk_hudless_capture.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

namespace Vulkan {
bool HudlessCapture::Enabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("DCPC_FG_HUDLESS");
        return FrameGenerator::Enabled() && (!value || std::atoi(value) != 0);
    }();
    return enabled;
}

void HudlessCapture::Register(VAddr address) {
    surfaces.try_emplace(address);
}

void HudlessCapture::OnDraw(Scheduler& scheduler, Runtime& runtime, VideoCore::Image& image,
                            VAddr address) {
    auto found = surfaces.find(address);
    if (found == surfaces.end())
        return;
    static const u32 capture_draw = [] {
        const char* value = std::getenv("DCPC_FG_HUD_DRAW");
        return value ? u32(std::clamp(std::atoi(value), 2, 32)) : 2u;
    }();
    if (++found->second.draws != capture_draw || image.backing_num_samples != 1)
        return;
    const vk::Extent2D size{image.info.size.width, image.info.size.height};
    const auto format = image.info.pixel_format;
    std::shared_ptr<HudlessSnapshot> snapshot;
    for (auto& item : pool) {
        if (item.use_count() == 1 && item->native_size == size && item->native_format == format) {
            snapshot = item;
            break;
        }
    }
    if (!snapshot) {
        if (pool.size() >= 8) {
            std::erase_if(pool, [](auto& item) { return item.use_count() == 1; });
            if (pool.size() >= 8)
                return;
        }
        snapshot = std::make_shared<HudlessSnapshot>();
        snapshot->native = VideoCore::UniqueImage(instance.GetDevice(), instance.GetAllocator());
        snapshot->native.Create(vk::ImageCreateInfo{
            .flags = vk::ImageCreateFlagBits::eMutableFormat,
            .imageType = vk::ImageType::e2D,
            .format = format,
            .extent = {size.width, size.height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled});
        snapshot->native_size = size;
        snapshot->native_format = format;
        pool.push_back(snapshot);
    }
    scheduler.EndRendering();
    runtime.Transit(&image, vk::ImageLayout::eTransferSrcOptimal,
                    vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead);
    runtime.FlushBarriers();
    const auto cmd = scheduler.CommandBuffer();
    vk::ImageMemoryBarrier2 barrier{
        .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .srcAccessMask = vk::AccessFlagBits2::eMemoryRead,
        .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eTransferDstOptimal,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = vk::Image(snapshot->native),
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
    cmd.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});
    const vk::ImageCopy region{.srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                               .dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                               .extent = {size.width, size.height, 1}};
    cmd.copyImage(image.GetImage(), vk::ImageLayout::eTransferSrcOptimal,
                  vk::Image(snapshot->native), vk::ImageLayout::eTransferDstOptimal, region);
    barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
    barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
    barrier.dstStageMask = vk::PipelineStageFlagBits2::eAllCommands;
    barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
    barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    cmd.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});
    runtime.Transit(&image, vk::ImageLayout::eColorAttachmentOptimal,
                    vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                    vk::AccessFlagBits2::eColorAttachmentRead |
                        vk::AccessFlagBits2::eColorAttachmentWrite);
    runtime.FlushBarriers();
    found->second.pending = std::move(snapshot);
    if (++captures == 1 || captures % 300 == 0) {
        LOG_INFO(Render_Vulkan, "driveclub-pc: pre-HUD captures {}, draw {}, {}x{}, format {}",
                 captures, capture_draw, size.width, size.height, vk::to_string(format));
    }
}

std::shared_ptr<HudlessSnapshot> HudlessCapture::Take(VAddr address) {
    auto found = surfaces.find(address);
    if (found == surfaces.end())
        return {};
    found->second.draws = 0;
    auto result = std::move(found->second.pending);
    if (!result && ++missing % 300 == 0) {
        LOG_INFO(Render_Vulkan, "driveclub-pc: pre-HUD missing {} flips; no stale snapshot reuse",
                 missing);
    }
    return result;
}
} // namespace Vulkan
