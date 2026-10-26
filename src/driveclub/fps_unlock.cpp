// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <vector>

#include "common/logging/log.h"
#include "driveclub/fps_unlock.h"

namespace DriveclubPC::FpsUnlock {

namespace {

struct Site {
    const char* name;
    u64 addr;
    std::vector<u8> original;
    std::vector<u8> patched;
};

// driveclub-pc: a private copy for the two main game clocks; explicit replay clocks
// keep the original helper. This padding is separate from render_res/widescreen stubs.
bool PatchSelectiveClocks(VAddr base, u64 size) {
    constexpr u64 function = 0x663040;
    constexpr u64 cave = 0x1552100;
    constexpr char original[] = "\x55\x48\x89\xe5\x53\x50\x48\x89\xfb\xe8\x22\x18\x00\x00\x48\x69"
                                "\xc0\x40\x42\x0f\x00\x31\xd2\x48\xf7\x35\x5a\x32\x2b\x01\x48\x8b"
                                "\x33\x48\x8d\x0d\x78\x63\xbe\x00\x48\x8b\x11\x48\x39\xd6\x75\x07"
                                "\x48\x89\xc6\x48\x2b\x73\x08\xc4\xe1\xfb\x2a\xc6\xc5\xfb\x59\x43"
                                "\x30\xc4\xe1\xfb\x2c\xf0\x80\x7b\x38\x00\x74\x0a\x48\x89\x53\x18"
                                "\x48\x89\x73\x20\xeb\x0f\x48\x89\x73\x18\x48\x8b\x09\x48\x89\x4b"
                                "\x20\x48\x01\x73\x10\x48\x89\x43\x08\x48\x83\xc4\x08\x5b\x5d\xc3";
    constexpr size_t length = sizeof(original) - 1;
    static_assert(length == 0x70);
    if (cave + length > size) {
        return false;
    }
    std::array<u8, length> source;
    std::memcpy(source.data(), reinterpret_cast<const void*>(base + function), length);
    // The existing three-site check has validated the optional deltatime NOPs.
    source[0x2e] = 0x75;
    source[0x2f] = 0x07;
    if (std::memcmp(source.data(), original, length) != 0) {
        return false;
    }
    auto clone = source;
    const auto rel32 = [](u8* out, u64 next, u64 target) {
        const s32 displacement = static_cast<s32>(static_cast<s64>(target) - next);
        std::memcpy(out, &displacement, sizeof(displacement));
    };
    rel32(clone.data() + 0x0a, cave + 0x0e, 0x664870);
    rel32(clone.data() + 0x1a, cave + 0x1e, 0x19162b8);
    rel32(clone.data() + 0x24, cave + 0x28, 0x12493e0);
    clone[0x2e] = clone[0x2f] = 0x90;
    auto* padding = reinterpret_cast<u8*>(base + cave);
    bool empty = true;
    for (size_t i = 0; i < length; ++i) {
        empty &= padding[i] == 0;
    }
    if (!empty && std::memcmp(padding, clone.data(), length) != 0) {
        return false;
    }
    constexpr std::array<u64, 2> calls = {0x78ef3, 0x78f2e};
    std::array<std::array<u8, 5>, 2> redirects;
    for (size_t i = 0; i < calls.size(); ++i) {
        std::array<u8, 5> expected = {0xe8};
        rel32(expected.data() + 1, calls[i] + 5, function);
        redirects[i][0] = 0xe8;
        rel32(redirects[i].data() + 1, calls[i] + 5, cave);
        const auto* p = reinterpret_cast<const void*>(base + calls[i]);
        if (std::memcmp(p, expected.data(), 5) != 0 &&
            std::memcmp(p, redirects[i].data(), 5) != 0) {
            return false;
        }
    }
    std::memcpy(padding, clone.data(), length);
    for (size_t i = 0; i < calls.size(); ++i) {
        std::memcpy(reinterpret_cast<void*>(base + calls[i]), redirects[i].data(), 5);
    }
    return true;
}

} // namespace

void Apply(VAddr base, u64 size) {
    const char* env = std::getenv("DCPC_FPS_UNLOCK");
    if (env && std::strcmp(env, "0") == 0) {
        LOG_INFO(Loader, "driveclub-pc: fps unlock disabled (DCPC_FPS_UNLOCK=0)");
        return;
    }
    const Site sites[] = {
        // Elapsed-time helper: always use real elapsed time instead of the fixed 30 FPS step.
        {"timer", 0x66306e, {0x75, 0x07}, {0x90, 0x90}},
        // fixedFramesPerSecond option check: setne al -> mov al, 1.
        {"fixed-fps", 0xd84725, {0x0f, 0x95, 0xc0}, {0x48, 0xb0, 0x01}},
        // Flip interval: return 1 (60 Hz) instead of 2 below 60 FPS.
        {"interval", 0xd84801, {0x02}, {0x01}},
    };
    // driveclub-pc: opt-in alternative from kalaposfos' v1.28 fixed-tickrate patch.
    // Validate the entire option name and all deltatime sites before changing any bytes.
    const char* fixed = std::getenv("DCPC_FIXED_TICKRATE");
    if (fixed && std::strcmp(fixed, "1") == 0) {
        constexpr u64 option_addr = 0x1362801;
        constexpr char original[] = "fixedFramesPerSecond";
        constexpr char patched[] = "gixedFramesPerSecond";
        if (option_addr + sizeof(original) > size) {
            LOG_ERROR(Loader, "driveclub-pc: fixed tickrate skipped, image too small");
            return;
        }
        auto* option = reinterpret_cast<void*>(base + option_addr);
        if (std::memcmp(option, original, sizeof(original)) != 0 &&
            std::memcmp(option, patched, sizeof(patched)) != 0) {
            LOG_ERROR(Loader, "driveclub-pc: fixed tickrate skipped, option name does not match");
            return;
        }
        const char* selective_env = std::getenv("DCPC_SELECTIVE_CLOCKS");
        const bool selective = selective_env && std::strcmp(selective_env, "1") == 0;
        for (const auto& s : sites) {
            if (s.addr + s.original.size() > size) {
                LOG_ERROR(Loader, "driveclub-pc: fixed tickrate skipped, image too small");
                return;
            }
            const auto* p = reinterpret_cast<const void*>(base + s.addr);
            if (std::memcmp(p, s.original.data(), s.original.size()) != 0 &&
                std::memcmp(p, s.patched.data(), s.patched.size()) != 0) {
                LOG_ERROR(Loader, "driveclub-pc: fixed tickrate skipped, {} bytes do not match",
                          s.name);
                return;
            }
        }
        if (selective && !PatchSelectiveClocks(base, size)) {
            LOG_ERROR(
                Loader,
                "driveclub-pc: selective clocks skipped, helper/call/padding signature mismatch");
            return;
        }
        for (const auto& s : sites) {
            std::memcpy(reinterpret_cast<void*>(base + s.addr), s.original.data(),
                        s.original.size());
        }
        std::memcpy(option, patched, sizeof(patched));
        if (selective) {
            LOG_INFO(Loader,
                     "driveclub-pc: experimental selective clocks applied: main +0x48/+0x88 use "
                     "real time, shared helper and replay clocks unchanged; replay unverified");
            return;
        }
        const char* realtime = std::getenv("DCPC_FIXED_TICKRATE_REALTIME");
        if (realtime && std::strcmp(realtime, "1") == 0) {
            // driveclub-pc: test real elapsed time without forcing the other deltatime sites.
            const auto& timer = sites[0];
            std::memcpy(reinterpret_cast<void*>(base + timer.addr), timer.patched.data(),
                        timer.patched.size());
            LOG_INFO(Loader,
                     "driveclub-pc: experimental fixed tickrate + real elapsed time applied; "
                     "fixed-fps check and flip interval remain original; speed/replay unverified");
            return;
        }
        LOG_INFO(Loader,
                 "driveclub-pc: experimental fixed tickrate applied; deltatime sites restored; "
                 "validate at target 60 before using other rates");
        return;
    }
    for (const auto& s : sites) {
        if (s.addr + s.original.size() > size) {
            return;
        }
    }
    int applied = 0, already = 0;
    for (const auto& s : sites) {
        u8* p = reinterpret_cast<u8*>(base + s.addr);
        if (std::memcmp(p, s.original.data(), s.original.size()) == 0) {
            ++applied;
        } else if (std::memcmp(p, s.patched.data(), s.patched.size()) == 0) {
            ++already;
        } else {
            LOG_INFO(Loader, "driveclub-pc: fps unlock skipped, bytes at {} (+{:#x}) do not match",
                     s.name, s.addr);
            return;
        }
    }
    if (already == 3) {
        LOG_INFO(Loader, "driveclub-pc: fps unlock already present in eboot");
        return;
    }
    for (const auto& s : sites) {
        std::memcpy(reinterpret_cast<u8*>(base + s.addr), s.patched.data(), s.patched.size());
    }
    LOG_INFO(Loader, "driveclub-pc: fps unlock applied ({} sites patched)", applied);
}

} // namespace DriveclubPC::FpsUnlock
