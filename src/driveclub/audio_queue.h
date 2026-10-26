// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <cstdlib>
#include "common/types.h"

namespace DriveclubPC {

inline bool AudioBufferingEnabled() {
    static const bool enabled = [] {
        const auto* env = std::getenv("DCPC_AUDIO_BUFFERING");
        return !env || env[0] != '0';
    }();
    return enabled;
}

constexpr u32 AudioStartupFrames(u32 sample_rate) {
    return (sample_rate * 32 + 999) / 1000;
}

constexpr u32 AudioRecoveryPackets(u32 frames, u32 sample_rate) {
    // driveclub-pc: 256-frame blocks last only 5.3 ms, shorter than a host mixer period.
    return std::max(2u, (AudioStartupFrames(sample_rate) + frames - 1) / frames);
}

} // namespace DriveclubPC
