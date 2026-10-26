// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>

#include "common/logging/log.h"
#include "common/types.h"

namespace DriveclubPC {

// driveclub-pc: opt-in observations of packet cadence and host queues, without recording audio.
class AudioStats {
public:
    explicit AudioStats(const char* backend) : backend{backend} {}

    static bool Enabled() {
        static const bool enabled = [] {
            const auto* env = std::getenv("DCPC_AUDIO_STATS");
            return env && env[0] == '1';
        }();
        return enabled;
    }

    void Record(const void* samples, u32 frames, u32 channels, u32 rate, bool is_float,
                u32 queued_frames, bool reset, bool dropped) {
        if (!Enabled()) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (packets == 0) {
            start = now;
            min_queue = std::numeric_limits<u32>::max();
        }
        if (last.time_since_epoch().count() != 0) {
            max_gap_ms =
                std::max(max_gap_ms, std::chrono::duration<double, std::milli>(now - last).count());
        }
        last = now;
        ++packets;
        total_frames += frames;
        min_queue = std::min(min_queue, queued_frames);
        max_queue = std::max(max_queue, queued_frames);
        empty_observations += queued_frames == 0;
        resets += reset;
        drops += dropped;
        for (size_t i = 0; i < size_t(frames) * channels; ++i) {
            const float value = is_float ? static_cast<const float*>(samples)[i]
                                         : static_cast<const s16*>(samples)[i] / 32768.f;
            if (!std::isfinite(value)) {
                ++nonfinite;
            } else {
                peak = std::max(peak, std::abs(value));
                over_one += std::abs(value) > 1.f;
            }
        }
        const double elapsed = std::chrono::duration<double>(now - start).count();
        if (elapsed < 5.0) {
            return;
        }
        LOG_INFO(Lib_AudioOut,
                 "driveclub-pc: audio stats {} port={} ch={} rate={} packets={} seconds={:.3f} "
                 "frames_per_s={:.1f} max_gap_ms={:.3f} queue_ms={:.2f}..{:.2f} "
                 "empty_obs={} resets={} drops={} peak={:.4f} over_one={} nonfinite={}",
                 backend, static_cast<const void*>(this), channels, rate, packets, elapsed,
                 (total_frames - frames) / elapsed, max_gap_ms, min_queue * 1000.0 / rate,
                 max_queue * 1000.0 / rate, empty_observations, resets, drops, peak, over_one,
                 nonfinite);
        packets = total_frames = empty_observations = resets = drops = nonfinite = over_one = 0;
        max_queue = 0;
        max_gap_ms = peak = 0;
    }

private:
    const char* backend;
    std::chrono::steady_clock::time_point start{}, last{};
    u64 packets = 0, total_frames = 0, empty_observations = 0, resets = 0, drops = 0;
    u64 nonfinite = 0, over_one = 0;
    u32 min_queue = 0, max_queue = 0;
    double max_gap_ms = 0;
    float peak = 0;
};

} // namespace DriveclubPC
