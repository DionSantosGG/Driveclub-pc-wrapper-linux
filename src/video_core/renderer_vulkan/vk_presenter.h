// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <condition_variable>
#include <deque>
#include <thread>
#include "video_core/renderer_vulkan/vk_frame_gen.h"
#include "video_core/renderer_vulkan/vk_hudless_capture.h"

#include "core/libraries/videoout/buffer.h"
#include "imgui/imgui_texture.h"
#include "video_core/renderer_vulkan/host_passes/aa_pass.h"
#include "video_core/renderer_vulkan/host_passes/fsr_pass.h"
#include "video_core/renderer_vulkan/host_passes/pp_pass.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_swapchain.h"
#include "video_core/texture_cache/texture_cache.h"

namespace Frontend {
class WindowSDL;
}

namespace AmdGpu {
struct Liverpool;
}

namespace Vulkan {

struct Frame {
    u32 width;
    u32 height;
    VmaAllocation allocation;
    vk::Image image;
    vk::ImageView image_view;
    vk::Fence present_done;
    vk::Semaphore ready_semaphore;
    u64 ready_tick;
    bool is_hdr{false};
    u8 id{};

    ImTextureID imgui_texture;
    std::shared_ptr<HudlessSnapshot> hudless;
};

enum SchedulerType {
    Draw,
    Present,
    CpuFlip,
};

class Rasterizer;

class Presenter {
public:
    Presenter(Frontend::WindowSDL& window, AmdGpu::Liverpool* liverpool);
    ~Presenter();

    HostPasses::PostProcessingPass::Settings& GetPPSettingsRef() {
        return pp_settings;
    }

    HostPasses::FsrPass::Settings& GetFsrSettingsRef() {
        return fsr_settings;
    }

    Frontend::WindowSDL& GetWindow() const {
        return window;
    }

    Rasterizer& GetRasterizer() const {
        return *rasterizer.get();
    }

    bool IsHDRSupported() const {
        return swapchain.HasHDR();
    }

    void SetHDR(bool enable) {
        if (!IsHDRSupported()) {
            return;
        }
        swapchain.SetHDR(enable);
        pp_settings.hdr = enable ? 1 : 0;
    }

    VideoCore::Image& RegisterVideoOutSurface(
        const Libraries::VideoOut::BufferAttributeGroup& attribute, VAddr cpu_address);

    bool IsVideoOutSurface(const AmdGpu::ColorBuffer& color_buffer) const;

    Frame* PrepareFrame(const Libraries::VideoOut::BufferAttributeGroup& attribute,
                        VAddr cpu_address);

    Frame* PrepareBlankFrame(bool present_thread);

    void Present(Frame* frame, bool is_reusing_frame = false, bool is_game_frame = true);
    Frame* PrepareLastFrame();

private:
    Frame* GetRenderFrame();
    void StartPacedPresent();
    void WaitPacedPresent();
    bool GenerateFrame(Frame* frame, bool eligible);
    void DumpFgPair(Frame* frame);
    void PrepareHudless(Frame* frame, VAddr address, vk::Format view_format,
                        bool srgb_encoded, int aa);
    void RecordGuiFrame(Frame* frame, vk::Image source, ImTextureID texture,
                        bool is_reusing_frame, bool screenshots);
    std::unique_ptr<FrameGenerator> frame_gen;
    std::chrono::steady_clock::time_point fg_last_real{}, fg_report_start{};
    float fg_interval_ms = 16.667f;
    bool fg_active = false;
    struct PacedJob {
        u32 gen_index{}, real_index{};
        u64 gen_tick{}, real_tick{};
        std::chrono::microseconds half{};
    };
    PacedJob fg_next_job{};
    std::thread fg_pacer;
    std::mutex fg_mutex;
    std::condition_variable fg_cv;
    std::deque<PacedJob> fg_jobs;
    bool fg_stop = false, fg_present_failed = false;
    u64 fg_real_count{}, fg_present_count{}, fg_pairs{}, fg_hudless_count{};
    double fg_gen_to_real_ms{}, fg_real_to_gen_ms{};
    std::chrono::steady_clock::time_point fg_real_at{};
    int fg_dump_left = 0, fg_dump_index = 0;
    int fg_dump_stage = 0;

    void RecreateFrame(Frame* frame, u32 width, u32 height);

    void SetExpectedGameSize(s32 width, s32 height);

private:
    float expected_ratio{1920.0 / 1080.0f};
    u32 expected_frame_width{1920};
    u32 expected_frame_height{1080};

    Frontend::WindowSDL& window;
    Instance instance;
    HostPasses::FsrPass fsr_pass;
    // driveclub-pc: AA consumes the game's internal resolution before FSR.
    HostPasses::AaPass aa_pass;
    int aa_mode = 1;
    HostPasses::FsrPass::Settings fsr_settings{};
    HostPasses::PostProcessingPass::Settings pp_settings{};
    HostPasses::PostProcessingPass pp_pass;
    AmdGpu::Liverpool* liverpool;
    Scheduler draw_scheduler;
    Scheduler present_scheduler;
    Scheduler flip_scheduler;
    Swapchain swapchain;
    Runtime runtime;
    std::unique_ptr<Rasterizer> rasterizer;
    VideoCore::TextureCache& texture_cache;
    vk::UniqueCommandPool command_pool;
    std::vector<Frame> present_frames;
    std::queue<Frame*> free_queue;
    Frame* last_submit_frame;
    std::mutex free_mutex;
    std::condition_variable free_cv;
    std::condition_variable_any frame_cv;
    std::optional<ImGui::RefCountedTexture> splash_img;
    std::vector<VAddr> vo_buffers_addr;
};

} // namespace Vulkan
