// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// driveclub-pc: the settings window shown when Driveclub-PC.exe starts without arguments.
//
// It picks the game folder, imports the user's own firmware modules, and sets the frame rate,
// display mode, resolution, upscaling and a few runtime options. Launcher settings live in
// user/driveclub_pc.ini; the graphics ones are written into the per-game emulator settings
// (user/custom_configs/<selected-title-id>.json), so they also apply when the game folder is passed
// on the command line. Hold Shift while starting to open the window even when it is turned off.

#pragma once

#include <filesystem>
#include <optional>

namespace DriveclubPC::Launcher {

/// Shows the window if needed and applies the chosen settings. Returns the game folder to
/// start, or nullopt when the user closed the window. Call after the emulator settings loaded.
std::optional<std::filesystem::path> Run();

/// Applies the saved launcher settings that are not part of the per-game config (frame rate,
/// runtime toggles and save root) when the game folder came from the command line.
/// Returns false if a custom save root cannot be used; check_only suppresses error dialogs.
bool ApplySavedRuntimeOptions(bool check_only = false);

} // namespace DriveclubPC::Launcher
