// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// driveclub-pc: front-end checks that run before the emulator core starts. Driveclub-PC.exe runs
// one game, so a wrong folder or missing firmware modules are reported up front in plain words
// instead of failing later inside the runtime.

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace DriveclubPC {

constexpr std::string_view TitleId = "CUSA00093";
constexpr std::string_view TargetAppVersion = "01.28";
constexpr std::string_view AppName = "Driveclub-PC";

// Accepts the base game folder, its update folder (<base>-UPDATE / <base>-patch) or an eboot.bin
// inside either. Returns the eboot.bin to start, or nullopt with a user-facing message in `error`.
std::optional<std::filesystem::path> ResolveAndValidateGame(const std::filesystem::path& arg,
                                                            std::string& error);

// driveclub-pc: read the selected region for configs, saves and firmware overrides.
std::optional<std::string> ReadGameTitleId(const std::filesystem::path& eboot_or_folder);
std::string GameCompatibilityNote(std::string_view title_id);

struct FirmwareReport {
    bool ready = false;
    std::string summary;
    std::string details;
};
FirmwareReport InspectSysModules(std::string_view title_id = TitleId);

// Firmware modules are the user's own (never shipped). InspectSysModules distinguishes required
// modules from observed optional modules with an HLE fallback, and checks basic file headers.
bool ValidateSysModules(std::string& error, std::string_view title_id = TitleId);

// Writes the Driveclub per-game settings once, when the user has none: the GPU readback settings
// recommended for the game (Precise/readbacks_mode=2, readback_offload=true), FSR 1 + RCAS, and
// borderless fullscreen at the primary display's resolution.
void EnsureDefaultGameConfig(std::string_view title_id = TitleId);

void ShowError(std::string_view message);

std::string UsageText();

} // namespace DriveclubPC
