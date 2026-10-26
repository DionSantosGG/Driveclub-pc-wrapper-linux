// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>
#include <vector>
#include "common/types.h"

namespace DriveclubPC {

constexpr u64 DeinterleaveShaderHash = 0x2d9c66a5;

enum class ShaderFixStatus {
    NotApplicable,
    Disabled,
    AlreadyDynamic,
    SignatureMismatch,
    Fixed,
};

struct ShaderFixResult {
    ShaderFixStatus status = ShaderFixStatus::NotApplicable;
    std::vector<u32> code;
};

// driveclub-pc: checked against the v1.28 guest shader and its SPIR-V shape. The
// bounds must follow the storage image, including when a cached module is loaded.
ShaderFixResult FixDeinterleaveBounds(std::span<const u32> code, u64 guest_hash, bool enabled);

} // namespace DriveclubPC
