// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/assert.h"
#include "common/io_file.h"
#include "common/logging/log.h"
#include "common/path_util.h"
#include "driveclub/shader_fixes.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"

#include <cstdlib>

namespace Vulkan {

vk::ShaderModule CompileSPV(std::span<const u32> code, vk::Device device, u64 guest_hash) {
    // driveclub-pc: also fix modules loaded from old caches; never modify game files or dumps.
    static const bool bounds_fix = [] {
        const auto* env = std::getenv("DCPC_SHADER_RES_FIX");
        return !env || env[0] != '0';
    }();
    auto fixed = DriveclubPC::FixDeinterleaveBounds(code, guest_hash, bounds_fix);
    if (fixed.status == DriveclubPC::ShaderFixStatus::Fixed) {
        code = fixed.code;
        LOG_INFO(Render_Vulkan,
                 "driveclub-pc: shader {:#x} deinterleave bounds now query the storage image size",
                 guest_hash);
        if (const auto* dump = std::getenv("DCPC_SHADER_FIX_DUMP"); dump && dump[0] == '1') {
            const auto path = Common::FS::GetUserPath(Common::FS::PathType::ShaderDir) /
                              "driveclub_deinterleave_fixed.spv";
            const Common::FS::IOFile file{path, Common::FS::FileAccessMode::Create};
            file.WriteSpan(code);
        }
    } else if (fixed.status == DriveclubPC::ShaderFixStatus::SignatureMismatch) {
        LOG_WARNING(Render_Vulkan,
                    "driveclub-pc: shader {:#x} bounds fix declined: unexpected SPIR-V layout",
                    guest_hash);
    }
    const vk::ShaderModuleCreateInfo shader_info = {
        .codeSize = code.size() * sizeof(u32),
        .pCode = code.data(),
    };

    auto [module_result, module] = device.createShaderModule(shader_info);
    ASSERT_MSG(module_result == vk::Result::eSuccess, "Failed to compile SPIR-V shader: {}",
               vk::to_string(module_result));
    return module;
}

} // namespace Vulkan
