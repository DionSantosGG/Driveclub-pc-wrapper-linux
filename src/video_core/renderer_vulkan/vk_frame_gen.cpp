// SPDX-FileCopyrightText: Copyright 2026 Killzone port contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdio>
#include <cstdlib>
#include <thread>
#include <imgui.h>
#include "imgui/renderer/imgui_impl_vulkan.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#ifdef _WIN32
#include <windows.h>
#endif

#include "video_core/renderer_vulkan/vk_frame_gen.h"
#include "video_core/renderer_vulkan/vk_instance.h"

#include <ffx_vk_portable.h>

namespace Vulkan {

namespace {

constexpr vk::Format SourceFormat = vk::Format::eR8G8B8A8Unorm;

FfxVkPortableImage Describe(vk::Image image, vk::Format format, u32 width, u32 height,
                            vk::ImageUsageFlags usage, FfxVkPortableResourceState state) {
    FfxVkPortableImage out{};
    out.structSize = sizeof(out);
    out.image = image;
    out.format = static_cast<VkFormat>(format);
    out.extent = {width, height};
    out.mipCount = 1;
    out.arrayLayers = 1;
    out.usage = static_cast<VkImageUsageFlags>(usage);
    out.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    out.state = state;
    return out;
}

constexpr vk::ImageSubresourceRange ColorRange{
    .aspectMask = vk::ImageAspectFlagBits::eColor,
    .levelCount = 1,
    .layerCount = 1,
};

void Transition(vk::CommandBuffer cmdbuf, vk::Image image, vk::ImageLayout from,
                vk::ImageLayout to) {
    const vk::ImageMemoryBarrier2 barrier{
        .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .srcAccessMask = vk::AccessFlagBits2::eMemoryWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .dstAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
        .oldLayout = from,
        .newLayout = to,
        .image = image,
        .subresourceRange = ColorRange,
    };
    cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});
}

const vk::ImageUsageFlags SourceUsage =
    vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage |
    vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst;
const vk::ImageUsageFlags OutputUsage = vk::ImageUsageFlagBits::eSampled |
                                        vk::ImageUsageFlagBits::eStorage |
                                        vk::ImageUsageFlagBits::eTransferSrc;
const vk::ImageUsageFlags InputUsage =
    vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;

} // namespace

void PreciseSleepUntil(std::chrono::steady_clock::time_point due) {
    using namespace std::chrono;
#ifdef _WIN32
    struct Timer {
        HANDLE handle = CreateWaitableTimerExW(
            nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        ~Timer() {
            if (handle)
                CloseHandle(handle);
        }
    };
    thread_local Timer owned_timer;
    const HANDLE timer = owned_timer.handle;
    const auto coarse = due - microseconds(300);
    const auto now = steady_clock::now();
    if (timer && coarse > now) {
        LARGE_INTEGER relative;
        relative.QuadPart =
            -static_cast<LONGLONG>(duration_cast<nanoseconds>(coarse - now).count() / 100);
        if (SetWaitableTimer(timer, &relative, 0, nullptr, nullptr, FALSE)) {
            WaitForSingleObject(timer, INFINITE);
        }
    }
#else
    if (due - steady_clock::now() > microseconds(1500)) {
        std::this_thread::sleep_until(due - microseconds(1500));
    }
#endif
    while (steady_clock::now() < due) {
        std::this_thread::yield();
    }
}

bool FrameGenerator::Enabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("DCPC_FRAME_GEN");
        return value && std::atoi(value) != 0;
    }();
    return enabled;
}

FrameGenerator::FrameGenerator(const Instance& instance_) : instance{instance_} {}

FrameGenerator::~FrameGenerator() {
    Destroy();
    if (queries) {
        instance.GetDevice().destroyQueryPool(queries);
    }
}

void FrameGenerator::Destroy() {
    if (context) {
        Scheduler::DrainAsyncSubmits();
        (void)instance.GetDevice().waitIdle();
        ffxVkPortableFrameGenerationContextDestroy(context);
        context = nullptr;
    }
    if (output_texture) {
        ImGui::Vulkan::RemoveTexture(output_texture);
        output_texture = {};
    }
    output_view.reset();
    hudless.Destroy();
    source.Destroy();
    output.Destroy();
    depth.Destroy();
    motion.Destroy();
    width = height = 0;
    inputs_cleared = false;
    have_previous = false;
}

bool FrameGenerator::Create(u32 w, u32 h) {
    Destroy();
    width = w;
    height = h;
    broken = true;

    FfxVkPortableDeviceInfo device_info{};
    device_info.structSize = sizeof(device_info);
    device_info.instance = instance.GetInstance();
    device_info.physicalDevice = instance.GetPhysicalDevice();
    device_info.device = instance.GetDevice();
    device_info.getDeviceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr;
    device_info.queue = instance.GetGraphicsQueue();
    device_info.queueFamilyIndex = instance.GetGraphicsQueueFamilyIndex();
    device_info.shaderFloat16Enabled = instance.IsShaderFloat16Supported();
    device_info.subgroupSizeControlEnabled = instance.IsSubgroupSizeControlEnabled();
    device_info.synchronization2Enabled = VK_TRUE;
    device_info.shaderStorageImageWriteWithoutFormatEnabled =
        instance.CanWriteStorageWithoutFormat();
    if (!device_info.shaderStorageImageWriteWithoutFormatEnabled) {
        LOG_ERROR(Render_Vulkan,
                  "Frame generation: required storage image writes unsupported; disabled");
        return false;
    }

    FfxVkPortableFrameGenerationCreateInfo create_info{};
    create_info.structSize = sizeof(create_info);
    create_info.flags = 0;
    create_info.maxRenderSize = {w, h};
    create_info.displaySize = {w, h};
    create_info.interpolationSourceFormat = static_cast<VkFormat>(SourceFormat);
    create_info.outputFormat = static_cast<VkFormat>(SourceFormat);
    if (const u64 issues = ffxVkPortableValidateFrameGenerationCreateInfo(&create_info)) {
        LOG_ERROR(Render_Vulkan, "Frame generation: invalid create info {:#x}", issues);
        return false;
    }
    const FfxVkPortableResult result =
        ffxVkPortableFrameGenerationContextCreate(&device_info, &create_info, &context);
    if (result != FFX_VK_PORTABLE_OK) {
        LOG_ERROR(Render_Vulkan, "Frame generation: FSR 3 context creation failed ({})",
                  int(result));
        context = nullptr;
        return false;
    }

    const vk::Device device = instance.GetDevice();
    const auto allocator = instance.GetAllocator();
    const auto make = [&](VideoCore::UniqueImage& image, vk::Format format,
                          vk::ImageUsageFlags usage) {
        image = VideoCore::UniqueImage(device, allocator);
        image.Create(vk::ImageCreateInfo{
            .imageType = vk::ImageType::e2D,
            .format = format,
            .extent = {w, h, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .usage = usage,
        });
    };
    make(source, SourceFormat, SourceUsage);
    make(hudless, SourceFormat, SourceUsage);
    make(output, SourceFormat, OutputUsage);
    make(depth, vk::Format::eR32Sfloat, InputUsage);
    make(motion, vk::Format::eR16G16Sfloat, InputUsage);
    output_view =
        device
            .createImageViewUnique(vk::ImageViewCreateInfo{.image = vk::Image(output),
                                                           .viewType = vk::ImageViewType::e2D,
                                                           .format = SourceFormat,
                                                           // driveclub-pc: SDK alpha is an inpainting
                                                           // weight, not presentation opacity.
                                                           .components = {.a = vk::ComponentSwizzle::eOne},
                                                           .subresourceRange = ColorRange})
            .value;
    output_texture =
        ImGui::Vulkan::AddTexture(output_view.get(), vk::ImageLayout::eShaderReadOnlyOptimal);
    broken = false;
    LOG_INFO(Render_Vulkan, "Frame generation: FSR 3 interpolation at {}x{} (optical flow only)", w,
             h);
    return true;
}

bool FrameGenerator::Record(vk::CommandBuffer cmdbuf, vk::Image src, u32 w, u32 h, float frame_ms,
                            bool reset, vk::Image hudless_src) {
    if (w == 0 || h == 0) {
        return false;
    }
    if (w != width || h != height) {
        if (!Create(w, h)) {
            ++failures;
            return false;
        }
        reset = true;
    } else if (broken || !context) {
        return false;
    }
    if (!inputs_cleared) {
        // Constant inputs: everything at the far plane, no motion.
        for (vk::Image image : {vk::Image(depth), vk::Image(motion)}) {
            Transition(cmdbuf, image, vk::ImageLayout::eUndefined,
                       vk::ImageLayout::eTransferDstOptimal);
            const vk::ClearColorValue value{
                std::array<float, 4>{image == vk::Image(depth) ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f}};
            cmdbuf.clearColorImage(image, vk::ImageLayout::eTransferDstOptimal, value, ColorRange);
            Transition(cmdbuf, image, vk::ImageLayout::eTransferDstOptimal,
                       vk::ImageLayout::eGeneral);
        }
        inputs_cleared = true;
        reset = true;
    }

    if (!queries) {
        queries = instance.GetDevice()
                      .createQueryPool({.queryType = vk::QueryType::eTimestamp, .queryCount = 4})
                      .value;
        cmdbuf.resetQueryPool(queries, 0, 4);
    }
    // The pair written two frames ago is done by now (frames ahead are bounded): read it.
    query_slot ^= 1;
    if (query_written[query_slot]) {
        u64 stamps[2]{};
        if (instance.GetDevice().getQueryPoolResults(
                queries, query_slot * 2, 2, sizeof(stamps), stamps, sizeof(u64),
                vk::QueryResultFlagBits::e64) == vk::Result::eSuccess) {
            gpu_ms += double(stamps[1] - stamps[0]) *
                      instance.GetPhysicalDevice().getProperties().limits.timestampPeriod / 1e6;
            ++gpu_samples;
        }
    }
    cmdbuf.resetQueryPool(queries, query_slot * 2, 2);
    cmdbuf.writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands, queries, query_slot * 2);

    const auto copy_color = [&](vk::Image from, vk::Image into) {
        Transition(cmdbuf, from, vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferSrcOptimal);
        Transition(cmdbuf, into, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
        const vk::ImageBlit blit{
            .srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            .srcOffsets = std::array{vk::Offset3D{0, 0, 0}, vk::Offset3D{s32(w), s32(h), 1}},
            .dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            .dstOffsets = std::array{vk::Offset3D{0, 0, 0}, vk::Offset3D{s32(w), s32(h), 1}},
        };
        cmdbuf.blitImage(from, vk::ImageLayout::eTransferSrcOptimal, into,
                         vk::ImageLayout::eTransferDstOptimal, blit, vk::Filter::eNearest);
        Transition(cmdbuf, from, vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eGeneral);
        Transition(cmdbuf, into, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eGeneral);
    };
    copy_color(src, vk::Image(source));
    if (hudless_src)
        copy_color(hudless_src, vk::Image(hudless));
    if (last_hudless != bool(hudless_src))
        reset = true;
    last_hudless = bool(hudless_src);
    Transition(cmdbuf, vk::Image(output), vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral);

    if (frame_ms <= 0.0f || frame_ms > 200.0f) {
        frame_ms = 16.6f;
        reset = true;
    }
    const auto source_image = Describe(vk::Image(source), SourceFormat, w, h, SourceUsage,
                                       FFX_VK_PORTABLE_RESOURCE_STATE_GENERIC_READ);
    const auto hudless_image = hudless_src
                                   ? Describe(vk::Image(hudless), SourceFormat, w, h, SourceUsage,
                                              FFX_VK_PORTABLE_RESOURCE_STATE_GENERIC_READ)
                                   : FfxVkPortableImage{};
    const auto& flow_source = hudless_src ? hudless_image : source_image;
    FfxVkPortableFrameGenerationPrepareInfo prepare{};
    prepare.structSize = sizeof(prepare);
    prepare.commandBuffer = cmdbuf;
    prepare.depth = Describe(vk::Image(depth), vk::Format::eR32Sfloat, w, h, InputUsage,
                             FFX_VK_PORTABLE_RESOURCE_STATE_GENERIC_READ);
    prepare.motionVectors = Describe(vk::Image(motion), vk::Format::eR16G16Sfloat, w, h, InputUsage,
                                     FFX_VK_PORTABLE_RESOURCE_STATE_GENERIC_READ);
    prepare.renderSize = {w, h};
    prepare.jitterOffset = {0.0f, 0.0f};
    prepare.motionVectorScale = {1.0f, 1.0f};
    prepare.frameTimeMilliseconds = frame_ms;
    prepare.cameraNear = 0.1f;
    prepare.cameraFar = 1000.0f;
    prepare.cameraVerticalFovRadians = 1.0f;
    prepare.viewSpaceToMeters = 1.0f;
    prepare.minLuminance = 0.0f;
    prepare.maxLuminance = 1.0f;
    prepare.transferFunction = FFX_VK_PORTABLE_TRANSFER_FUNCTION_SRGB;
    prepare.cameraUp = {0.0f, 1.0f, 0.0f};
    prepare.cameraRight = {1.0f, 0.0f, 0.0f};
    prepare.cameraForward = {0.0f, 0.0f, 1.0f};
    prepare.reset = reset ? VK_TRUE : VK_FALSE;
    prepare.frameId = ++frame_id;
    if (ffxVkPortableFrameGenerationContextPrepare(context, &prepare, &flow_source) !=
        FFX_VK_PORTABLE_OK) {
        if (failures++ < 4) {
            LOG_ERROR(Render_Vulkan, "Frame generation: prepare failed");
        }
        return false;
    }

    FfxVkPortableFrameGenerationDispatchInfo dispatch{};
    dispatch.structSize = sizeof(dispatch);
    dispatch.commandBuffer = cmdbuf;
    dispatch.currentColor = source_image;
    dispatch.hudlessColor = hudless_image;
    dispatch.hudlessColor.structSize = sizeof(dispatch.hudlessColor);
    dispatch.distortionField.structSize = sizeof(dispatch.distortionField);
    dispatch.output = Describe(vk::Image(output), SourceFormat, w, h, OutputUsage,
                               FFX_VK_PORTABLE_RESOURCE_STATE_UNORDERED_ACCESS);
    dispatch.displaySize = {w, h};
    dispatch.interpolationRect = {0, 0, w, h};
    dispatch.frameTimeMilliseconds = frame_ms;
    dispatch.cameraNear = prepare.cameraNear;
    dispatch.cameraFar = prepare.cameraFar;
    dispatch.cameraVerticalFovRadians = prepare.cameraVerticalFovRadians;
    dispatch.viewSpaceToMeters = prepare.viewSpaceToMeters;
    dispatch.minLuminance = prepare.minLuminance;
    dispatch.maxLuminance = prepare.maxLuminance;
    dispatch.transferFunction = prepare.transferFunction;
    dispatch.reset = prepare.reset;
    dispatch.frameId = prepare.frameId;
    if (ffxVkPortableFrameGenerationContextRecordDispatch(context, &dispatch) !=
        FFX_VK_PORTABLE_OK) {
        if (failures++ < 4) {
            LOG_ERROR(Render_Vulkan, "Frame generation: dispatch failed");
        }
        return false;
    }
    const vk::MemoryBarrier2 done{.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
                                  .srcAccessMask = vk::AccessFlagBits2::eMemoryWrite,
                                  .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
                                  .dstAccessMask = vk::AccessFlagBits2::eMemoryRead};
    cmdbuf.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &done});
    cmdbuf.writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands, queries, query_slot * 2 + 1);
    query_written[query_slot] = true;
    // The first frame after a reset has no previous frame to interpolate from.
    const bool show = have_previous && !reset;
    have_previous = true;
    generated += show;
    return show;
}

} // namespace Vulkan
