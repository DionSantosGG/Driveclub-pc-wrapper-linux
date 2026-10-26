// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "common/logging/log.h"
#include "common/path_util.h"
#include "core/emulator_settings.h"
#include "driveclub/launch_check.h"
#include "driveclub/launcher.h"
#include "driveclub/widescreen.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// windows.h must precede the control and shell headers.
#include <commctrl.h>
#include <shellapi.h>
#include <shobjidl.h>
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#endif

namespace DriveclubPC::Launcher {

namespace {

namespace fs = std::filesystem;

fs::path TextPath(std::string_view text) {
    return fs::path(std::u8string(text.begin(), text.end()));
}

struct Settings {
    std::string game_dir;
    std::string save_dir; // driveclub-pc: empty uses the existing emulator home, saves only
    int target_fps = 60;
    bool fullscreen = true;
    int width = 0; ///< 0 = the primary display's resolution
    int height = 0;
    bool fsr = true;
    int sharpening = 1; ///< 0 off, 1 normal, 2 strong
    int readbacks = 2;  ///< driveclub-pc: 0 off, 1 relaxed, 2 precise (default: Relaxed broke
                        ///< shadows and vegetation in the owner's races)
    // driveclub-pc: image quality and hybrid CPU settings.
    int af = 16;
    int aa = 1;
    bool pcore_pin = true;
    // driveclub-pc: the game's own render scaling (render_res.h): 1080, 1440 or 2160 lines.
    int render_res = 1080;
    bool async_shaders = true; ///< shader translation and pipeline creation off-thread
    bool frame_generation = false;
    bool show_fps = false;
    bool deferred_recording = false;
    bool classic_vblank = false;
    bool show_launcher = true;
};

fs::path IniPath() {
    return Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "driveclub_pc.ini";
}

Settings Load() {
    Settings s;
    std::ifstream in(IniPath());
    std::map<std::string, std::string> kv;
    for (std::string line; std::getline(in, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF) {
            line.erase(0, 3); // UTF-8 byte order mark
        }
        const auto eq = line.find('=');
        if (eq != std::string::npos && line[0] != '#') {
            kv[line.substr(0, eq)] = line.substr(eq + 1);
        }
    }
    const auto num = [&](const char* key, int fallback) {
        const auto it = kv.find(key);
        return it == kv.end() ? fallback : std::atoi(it->second.c_str());
    };
    if (kv.contains("game_dir")) {
        s.game_dir = kv["game_dir"];
    }
    if (kv.contains("save_dir")) {
        s.save_dir = kv["save_dir"];
    }
    s.target_fps = num("target_fps", s.target_fps);
    s.fullscreen = num("fullscreen", s.fullscreen) != 0;
    s.width = num("width", s.width);
    s.height = num("height", s.height);
    s.fsr = num("fsr", s.fsr) != 0;
    s.sharpening = std::clamp(num("sharpening", s.sharpening), 0, 2);
    s.readbacks = std::clamp(num("readbacks", s.readbacks), 0, 2);
    s.af = num("af", s.af);
    if (s.af != 2 && s.af != 4 && s.af != 8 && s.af != 16)
        s.af = 0;
    s.aa = std::clamp(num("aa", s.aa), 0, 2);
    s.pcore_pin = num("pcore_pin", 1) != 0;
    s.render_res = num("render_res", s.render_res);
    if (s.render_res != 1440 && s.render_res != 2160) {
        s.render_res = 1080;
    }
    s.async_shaders = num("async_shaders", 1) != 0;
    s.frame_generation = num("frame_generation", 0) != 0;
    s.show_fps = num("show_fps", s.show_fps) != 0;
    s.deferred_recording = num("deferred_recording", s.deferred_recording) != 0;
    s.classic_vblank = num("classic_vblank", s.classic_vblank) != 0;
    s.show_launcher = num("show_launcher", s.show_launcher) != 0;
    if (s.target_fps != 30 && s.target_fps != 60 && s.target_fps != 120) {
        s.target_fps = 60;
    }
    // driveclub-pc: FG is the dedicated 60-FPS-base mode in the launcher.
    s.frame_generation = s.frame_generation && s.target_fps == 60;
    return s;
}

void Save(const Settings& s) {
    std::ofstream out(IniPath());
    out << "# Driveclub-PC launcher settings\n"
        << "game_dir=" << s.game_dir << "\n"
        << "save_dir=" << s.save_dir << "\n"
        << "target_fps=" << s.target_fps << "\n"
        << "fullscreen=" << int(s.fullscreen) << "\n"
        << "width=" << s.width << "\n"
        << "height=" << s.height << "\n"
        << "fsr=" << int(s.fsr) << "\n"
        << "sharpening=" << s.sharpening << "\n"
        << "readbacks=" << s.readbacks << "\n"
        << "af=" << s.af << "\n"
        << "aa=" << s.aa << "\n"
        << "pcore_pin=" << int(s.pcore_pin) << "\n"
        << "render_res=" << s.render_res << "\n"
        << "async_shaders=" << int(s.async_shaders) << "\n"
        << "frame_generation=" << int(s.frame_generation) << "\n"
        << "show_fps=" << int(s.show_fps) << "\n"
        << "deferred_recording=" << int(s.deferred_recording) << "\n"
        << "classic_vblank=" << int(s.classic_vblank) << "\n"
        << "show_launcher=" << int(s.show_launcher) << "\n";
}

std::pair<int, int> DisplaySize() {
    int width = 1920, height = 1080;
#ifdef _WIN32
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) && mode.dmPelsWidth > 0) {
        width = static_cast<int>(mode.dmPelsWidth);
        height = static_cast<int>(mode.dmPelsHeight);
    }
#endif
    return {width, height};
}

/// driveclub-pc: the game's output size: the render resolution lines at the display's aspect
/// in fullscreen or the window's aspect in windowed mode (wider than 16:9: widescreen.h).
Widescreen::Output TargetOutput(const Settings& s) {
    auto [w, h] = DisplaySize();
    if (!s.fullscreen && s.width > 0 && s.height > 0) {
        w = s.width;
        h = s.height;
    }
    return Widescreen::ComputeOutput(w, h, static_cast<u32>(s.render_res));
}

/// driveclub-pc: extra video memory the game needs for its render targets at this size.
int ExtraVramMb(const Widescreen::Output& out) {
    const u64 pixels = u64{out.width} * out.height;
    return pixels <= 1920u * 1080u ? 0 : pixels <= 2560u * 1440u ? 1024 : 1536;
}

/// Writes the graphics settings into the per-game emulator config, keeping its other keys.
void WriteGameConfig(const Settings& s) {
    const auto title_id = ReadGameTitleId(TextPath(s.game_dir)).value_or(std::string(TitleId));
    EnsureDefaultGameConfig(title_id);
    const auto path = Common::FS::GetUserPath(Common::FS::PathType::CustomConfigs) /
                      fmt::format("{}.json", title_id);
    nlohmann::json json = nlohmann::json::object();
    {
        std::ifstream in(path);
        if (in) {
            json = nlohmann::json::parse(in, nullptr, false);
            if (json.is_discarded() || !json.is_object()) {
                json = nlohmann::json::object();
            }
        }
    }
    const auto [dw, dh] = DisplaySize();
    auto& gpu = json["GPU"];
    gpu["full_screen"] = s.fullscreen;
    gpu["full_screen_mode"] = s.fullscreen ? "Fullscreen (Borderless)" : "Windowed";
    gpu["window_width"] = s.width > 0 ? s.width : dw;
    gpu["window_height"] = s.height > 0 ? s.height : dh;
    gpu["fsr_enabled"] = s.fsr;
    gpu["rcas_enabled"] = s.sharpening != 0;
    gpu["rcas_attenuation"] = s.sharpening == 2 ? 100 : 250;
    gpu["readbacks_mode"] = s.readbacks;
    // driveclub-pc: a higher render resolution needs a bigger video-memory budget in the game
    // (DCPC_VRAM_EXTRA_MB below), backed by extra direct memory.
    json["General"]["extra_dmem_in_mbytes"] = ExtraVramMb(TargetOutput(s));
    std::ofstream out(path);
    out << json.dump(2) << "\n";
}

void SetEnvDefault(const char* name, const std::string& value) {
    // A value already in the environment (test scripts) wins.
    if (std::getenv(name) == nullptr) {
#ifdef _WIN32
        _putenv_s(name, value.c_str());
#else
        setenv(name, value.c_str(), 0);
#endif
    }
}

void ApplyRuntime(const Settings& s) {
    EmulatorSettings.SetSaveDataHome(TextPath(s.save_dir));
    SetEnvDefault("DCPC_AF", std::to_string(s.af));
    SetEnvDefault("DCPC_AA", std::to_string(s.aa));
    SetEnvDefault("DCPC_PCORE_PIN", s.pcore_pin ? "1" : "0");
    SetEnvDefault("DCPC_FRAME_GEN", s.frame_generation ? "1" : "0");
    SetEnvDefault("DCPC_ASYNC_SHADERS", s.async_shaders ? "1" : "0");
    SetEnvDefault("DCPC_ASYNC_PIPELINES", s.async_shaders ? "1" : "0");
    const auto out = TargetOutput(s);
    if (out.height != 1080) {
        // The engine's own -resolutionscale switch scales its render targets (width aligned to
        // 128, height = width * 9 / 16, or table height * scale with widescreen).
        SetEnvDefault("DCPC_GAME_ARGS",
                      out.height == 2160 ? "-resolutionscale 2" : "-resolutionscale 1.3333");
    }
    if (out.wide || out.height != 1080) {
        // The game's display buffers follow DCPC_RENDER_RES.
        SetEnvDefault("DCPC_VRAM_EXTRA_MB", std::to_string(ExtraVramMb(out)));
        SetEnvDefault("DCPC_RENDER_RES", fmt::format("{}x{}", out.width, out.height));
    }
    if (out.wide) {
        // Aspect, Hor+ FOV and a wide scene (widescreen.h).
        SetEnvDefault("DCPC_WIDESCREEN", fmt::format("{}x{}", out.width, out.height));
    }
    SetEnvDefault("DCPC_TARGET_FPS", std::to_string(s.target_fps));
    SetEnvDefault("DCPC_DEFERRED_RECORDING", s.deferred_recording ? "1" : "0");
    SetEnvDefault("DCPC_CLASSIC_VBLANK", s.classic_vblank ? "1" : "0");
    if (s.show_fps) {
        EmulatorSettings.SetShowFpsCounter(true);
    }
}

fs::path SaveRoot(const Settings& s) {
    return s.save_dir.empty() ? EmulatorSettings.GetHomeDir() : TextPath(s.save_dir);
}

std::string PathText(const fs::path& path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

// driveclub-pc: validate before launching; never silently fall back to a different save root.
bool PrepareSaveRoot(const Settings& s, std::string& error) {
    if (s.save_dir.empty()) {
        return true;
    }
    const auto root = SaveRoot(s);
    if (!root.is_absolute()) {
        error = "The save data root must be an absolute path. Choose a folder in the launcher.";
        return false;
    }
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec || !fs::is_directory(root, ec)) {
        error = fmt::format("Cannot use save data root:\n{}\n{}", PathText(root), ec.message());
        return false;
    }
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto probe = root / fmt::format(".dcpc-write-check-{}", stamp);
    // Creating a uniquely named empty directory checks write access without touching save files.
    if (!fs::create_directory(probe, ec)) {
        error =
            fmt::format("Save data root is not writable:\n{}\n{}", PathText(root), ec.message());
        return false;
    }
    fs::remove(probe, ec);
    if (ec) {
        error = fmt::format("Cannot remove save directory write check:\n{}\n{}", PathText(probe),
                            ec.message());
        return false;
    }
    LOG_INFO(Loader, "driveclub-pc: custom save data root {} (existing saves are not moved)",
             PathText(root));
    return true;
}

std::string FirmwareStatus(bool& ok, std::string_view title_id = TitleId) {
    const auto report = InspectSysModules(title_id);
    ok = report.ready;
    return report.summary;
}

#ifdef _WIN32

std::wstring Widen(const std::string& s) {
    if (s.empty()) {
        return {};
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(n > 0 ? n - 1 : 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

std::string Narrow(const std::wstring& w) {
    if (w.empty()) {
        return {};
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

enum Id : int {
    IdGameEdit = 100,
    IdBrowse,
    IdGameStatus,
    IdFirmwareStatus,
    IdImport,
    IdFps,
    IdDisplay,
    IdResolution,
    IdRenderRes,
    IdUpscaling,
    IdSharpening,
    IdReadbacks,
    IdAf,
    IdAa,
    IdPcore,
    IdAsyncShaders,
    IdShowFps,
    IdDeferred,
    IdClassic,
    IdShowLauncher,
    IdPlay,
    IdQuit,
    IdSavePath,
    IdSaveBrowse,
    IdSaveOpen,
    IdSaveDefault,
    IdFirmwareDetails,
};

struct Ui {
    Settings settings;
    HWND window{};
    HFONT font{}, title_font{};
    std::map<int, HWND> controls;
    bool game_ok = false;
    bool firmware_ok = false;
    bool play = false;
    float scale = 1.0f;
    std::vector<std::pair<int, int>> resolutions;
    int scroll = 0;
    int content_height = 0;
};

Ui* g_ui = nullptr;

int Px(float dip) {
    return static_cast<int>(dip * g_ui->scale + 0.5f);
}

std::optional<std::wstring> PickFolder(HWND owner, const wchar_t* title) {
    std::optional<std::wstring> result;
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog)))) {
        return result;
    }
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dialog->SetTitle(title);
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                result = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return result;
}

std::string GetText(int id) {
    const HWND h = g_ui->controls[id];
    const int n = GetWindowTextLengthW(h);
    std::wstring w(static_cast<size_t>(n), L'\0');
    GetWindowTextW(h, w.data(), n + 1);
    return Narrow(w);
}

void SetText(int id, const std::string& text) {
    SetWindowTextW(g_ui->controls[id], Widen(text).c_str());
}

void RefreshStatus() {
    auto& ui = *g_ui;
    const std::string dir = GetText(IdGameEdit);
    std::string message;
    if (dir.empty()) {
        ui.game_ok = false;
        message = "Choose a Driveclub base game folder. Tested: CUSA00093 v1.28.";
    } else {
        std::string error;
        const auto eboot = ResolveAndValidateGame(TextPath(dir), error);
        ui.game_ok = eboot.has_value();
        message =
            ui.game_ok
                ? GameCompatibilityNote(ReadGameTitleId(*eboot).value_or(std::string(TitleId)))
                : error;
    }
    std::replace(message.begin(), message.end(), '\n', ' ');
    SetText(IdGameStatus, message);
    bool fw = false;
    const auto title_id = ReadGameTitleId(TextPath(dir)).value_or(std::string(TitleId));
    std::string fw_text = FirmwareStatus(fw, title_id);
    std::replace(fw_text.begin(), fw_text.end(), '\n', ' ');
    ui.firmware_ok = fw;
    SetText(IdFirmwareStatus, fw_text);
    EnableWindow(ui.controls[IdPlay], ui.game_ok && ui.firmware_ok);
    InvalidateRect(ui.window, nullptr, TRUE);
}

void RefreshSavePath() {
    SetText(IdSavePath, PathText(SaveRoot(g_ui->settings)));
}

void OpenSaveFolder() {
    std::string error;
    if (!PrepareSaveRoot(g_ui->settings, error)) {
        MessageBoxW(g_ui->window, Widen(error).c_str(), L"Driveclub-PC save data",
                    MB_OK | MB_ICONERROR);
        return;
    }
    const auto path = SaveRoot(g_ui->settings);
    std::error_code ec;
    fs::create_directories(path, ec);
    if (ec || reinterpret_cast<INT_PTR>(ShellExecuteW(g_ui->window, L"open", path.c_str(), nullptr,
                                                      nullptr, SW_SHOWNORMAL)) <= 32) {
        MessageBoxW(g_ui->window,
                    L"Cannot open the save data folder. Check the path and permissions.",
                    L"Driveclub-PC", MB_OK | MB_ICONERROR);
    }
}

void ImportFirmware() {
    const auto source = PickFolder(g_ui->window, L"Folder with your PS4 firmware modules (*.sprx)");
    if (!source) {
        return;
    }
    const fs::path dest = EmulatorSettings.GetSysModulesDir();
    std::error_code ec;
    fs::create_directories(dest, ec);
    int copied = 0, skipped = 0, failed = 0;
    for (const auto& entry : fs::directory_iterator(*source, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".sprx") {
            std::error_code copy_ec;
            if (fs::copy_file(entry.path(), dest / entry.path().filename(),
                              fs::copy_options::skip_existing, copy_ec)) {
                ++copied;
            } else if (copy_ec) {
                ++failed;
            } else {
                ++skipped;
            }
        }
    }
    MessageBoxW(g_ui->window,
                Widen(fmt::format("Copied {} module(s), kept {} existing, {} failed.\n{}\n\n"
                                  "See Details for missing or invalid modules. Existing files "
                                  "are never overwritten.",
                                  copied, skipped, failed, PathText(dest)))
                    .c_str(),
                L"Driveclub-PC", MB_OK | MB_ICONINFORMATION);
    RefreshStatus();
}

int ComboIndex(int id) {
    return static_cast<int>(SendMessageW(g_ui->controls[id], CB_GETCURSEL, 0, 0));
}

bool Checked(int id) {
    return SendMessageW(g_ui->controls[id], BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void CollectSettings() {
    auto& s = g_ui->settings;
    s.game_dir = GetText(IdGameEdit);
    static constexpr int fps_values[] = {30, 60, 60, 120};
    const int frame_mode = std::clamp(ComboIndex(IdFps), 0, 3);
    s.target_fps = fps_values[frame_mode];
    s.frame_generation = frame_mode == 2;
    s.fullscreen = ComboIndex(IdDisplay) == 0;
    const int res =
        std::clamp(ComboIndex(IdResolution), 0, static_cast<int>(g_ui->resolutions.size()) - 1);
    s.width = g_ui->resolutions[res].first;
    s.height = g_ui->resolutions[res].second;
    s.fsr = ComboIndex(IdUpscaling) == 0;
    s.sharpening = std::clamp(ComboIndex(IdSharpening), 0, 2);
    s.readbacks = 2 - std::clamp(ComboIndex(IdReadbacks), 0, 2);
    static constexpr int af_values[] = {0, 2, 4, 8, 16};
    s.af = af_values[std::clamp(ComboIndex(IdAf), 0, 4)];
    s.aa = std::clamp(ComboIndex(IdAa), 0, 2);
    s.pcore_pin = Checked(IdPcore);
    static constexpr int render_values[] = {1080, 1440, 2160};
    s.render_res = render_values[std::clamp(ComboIndex(IdRenderRes), 0, 2)];
    s.async_shaders = Checked(IdAsyncShaders);
    s.show_fps = Checked(IdShowFps);
    s.deferred_recording = Checked(IdDeferred);
    s.classic_vblank = Checked(IdClassic);
    s.show_launcher = Checked(IdShowLauncher);
}

/// driveclub-pc: the window size only applies in windowed mode.
void UpdateWindowSizeEnabled() {
    const auto it = g_ui->controls.find(IdResolution);
    if (it != g_ui->controls.end()) {
        EnableWindow(it->second, ComboIndex(IdDisplay) == 1);
    }
}

HWND Make(const wchar_t* cls, const wchar_t* text, DWORD style, float x, float y, float w, float h,
          int id, DWORD ex_style = 0) {
    HWND hwnd =
        CreateWindowExW(ex_style, cls, text, WS_CHILD | WS_VISIBLE | style, Px(x), Px(y), Px(w),
                        Px(h), g_ui->window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui->font), TRUE);
    if (id > 0) {
        g_ui->controls[id] = hwnd;
    }
    return hwnd;
}

HWND Combo(int id, float x, float y, float w, const std::vector<std::wstring>& items, int sel) {
    HWND h = Make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, x, y, w, 200, id);
    for (const auto& item : items) {
        SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
    }
    SendMessageW(h, CB_SETCURSEL, sel, 0);
    return h;
}

void Label(const wchar_t* text, float x, float y, float w, float h = 20) {
    Make(L"STATIC", text, SS_LEFT, x, y + 3, w, h, 0);
}

void BuildControls() {
    auto& ui = *g_ui;
    const auto& s = ui.settings;
    constexpr float L = 24, C = 170, W = 400, Row = 36;
    float y = 72;

    Label(L"Game folder", L, y, 140);
    Make(L"EDIT", Widen(s.game_dir).c_str(), ES_AUTOHSCROLL | WS_TABSTOP, C, y, W - 96, 24,
         IdGameEdit, WS_EX_CLIENTEDGE);
    Make(L"BUTTON", L"Browse...", BS_PUSHBUTTON | WS_TABSTOP, C + W - 88, y - 1, 88, 26, IdBrowse);
    y += 28;
    Make(L"STATIC", L"", SS_LEFT, C, y, W, 34, IdGameStatus);
    y += 40;

    Label(L"Firmware", L, y, 140);
    Make(L"STATIC", L"", SS_LEFT, C, y + 3, W, 34, IdFirmwareStatus);
    y += 38;
    Make(L"BUTTON", L"Import...", BS_PUSHBUTTON | WS_TABSTOP, C, y, 96, 26, IdImport);
    Make(L"BUTTON", L"Details...", BS_PUSHBUTTON | WS_TABSTOP, C + 104, y, 96, 26,
         IdFirmwareDetails);
    y += 38;

    Label(L"Save data root", L, y, 140);
    Make(L"EDIT", Widen(PathText(SaveRoot(s))).c_str(), ES_READONLY | ES_AUTOHSCROLL | WS_TABSTOP,
         C, y, W, 24, IdSavePath, WS_EX_CLIENTEDGE);
    y += 30;
    Make(L"BUTTON", L"Browse...", BS_PUSHBUTTON | WS_TABSTOP, C, y, 96, 26, IdSaveBrowse);
    Make(L"BUTTON", L"Open folder", BS_PUSHBUTTON | WS_TABSTOP, C + 104, y, 104, 26, IdSaveOpen);
    Make(L"BUTTON", L"Use default", BS_PUSHBUTTON | WS_TABSTOP, C + 216, y, 104, 26, IdSaveDefault);
    y += 30;
    Label(L"Saves: <root>\\<user-id>\\savedata\\<game-region>.\nExisting saves are not moved.", C,
          y, W, 36);
    y += 46;

    Label(L"Frame rate", L, y, 140);
    Combo(IdFps, C, y, W,
          {L"30 FPS (as on PS4)", L"60 FPS (recommended)", L"60 FPS + FG (2x, ~120 output)",
           L"120 FPS (experimental)"},
          s.target_fps == 30    ? 0
          : s.target_fps == 120 ? 3
          : s.frame_generation ? 2
                                : 1);
    y += Row;

    Label(L"Display", L, y, 140);
    Combo(IdDisplay, C, y, W, {L"Fullscreen (borderless)", L"Window"}, s.fullscreen ? 0 : 1);
    y += Row;

    const auto [dw, dh] = DisplaySize();
    // driveclub-pc: the three ultrawide sizes get a matching wide output (widescreen.h).
    ui.resolutions = {{0, 0},       {5120, 1440}, {3840, 2160}, {3440, 1440},
                      {2560, 1600}, {2560, 1440}, {2560, 1080}, {1920, 1200},
                      {1920, 1080}, {1600, 900},  {1280, 720}};
    std::vector<std::wstring> res_items;
    int res_sel = 0;
    for (size_t i = 0; i < ui.resolutions.size(); ++i) {
        const auto [w, h] = ui.resolutions[i];
        res_items.push_back(i == 0 ? Widen(fmt::format("Same as the display ({}x{})", dw, dh))
                                   : Widen(fmt::format("{}x{}", w, h)));
        if (w == s.width && h == s.height) {
            res_sel = static_cast<int>(i);
        }
    }
    // driveclub-pc: borderless fullscreen always covers the whole display; this is only the size
    // of the window in windowed mode.
    Label(L"Window size", L, y, 140);
    Combo(IdResolution, C, y, W, res_items, res_sel);
    UpdateWindowSizeEnabled();
    y += Row;

    // driveclub-pc: lines; the width follows the display (or window) aspect, see widescreen.h.
    Label(L"Render resolution", L, y, 140);
    Combo(IdRenderRes, C, y, W,
          {L"1080p (native, fastest)", L"1440p (sharper, about 10-15% slower)",
           L"2160p (very demanding)"},
          s.render_res == 2160   ? 2
          : s.render_res == 1440 ? 1
                                 : 0);
    y += Row;

    Label(L"Upscaling", L, y, 140);
    Combo(IdUpscaling, C, y, W, {L"AMD FSR 1 (game image to window size)", L"Bilinear"},
          s.fsr ? 0 : 1);
    y += Row;

    Label(L"Sharpening", L, y, 140);
    Combo(IdSharpening, C, y, W, {L"Off", L"Normal (RCAS)", L"Strong (RCAS)"}, s.sharpening);
    y += Row;

    Label(L"Anisotropic filter", L, y, 140);
    Combo(IdAf, C, y, W, {L"Off (game setting)", L"2x", L"4x", L"8x", L"16x"},
          s.af == 16  ? 4
          : s.af == 8 ? 3
          : s.af == 4 ? 2
          : s.af == 2 ? 1
                      : 0);
    y += Row;
    Label(L"Anti-aliasing", L, y, 140);
    Combo(IdAa, C, y, W, {L"Off", L"FXAA (default)", L"SMAA 1x (High)"}, s.aa);
    y += Row;

    Label(L"GPU readbacks", L, y, 140);
    Combo(IdReadbacks, C, y, W,
          {L"Precise (default)", L"Relaxed (faster, shadow/vegetation glitches)",
           L"Off (fastest, effects may break)"},
          2 - s.readbacks);
    y += Row + 6;

    Make(L"BUTTON", L"Prefer performance cores (hybrid CPUs)", BS_AUTOCHECKBOX | WS_TABSTOP, C, y,
         W, 22, IdPcore);
    SendMessageW(ui.controls[IdPcore], BM_SETCHECK, s.pcore_pin ? BST_CHECKED : BST_UNCHECKED, 0);
    y += 26;
    Make(L"BUTTON", L"Compile shaders in the background (less stutter)",
         BS_AUTOCHECKBOX | WS_TABSTOP, C, y, W, 22, IdAsyncShaders);
    SendMessageW(ui.controls[IdAsyncShaders], BM_SETCHECK,
                 s.async_shaders ? BST_CHECKED : BST_UNCHECKED, 0);
    y += 26;
    Make(L"BUTTON", L"Show FPS counter", BS_AUTOCHECKBOX | WS_TABSTOP, C, y, W, 22, IdShowFps);
    SendMessageW(ui.controls[IdShowFps], BM_SETCHECK, s.show_fps ? BST_CHECKED : BST_UNCHECKED, 0);
    y += 26;
    Make(L"BUTTON", L"Experimental: record Vulkan commands on a second thread",
         BS_AUTOCHECKBOX | WS_TABSTOP, C, y, W, 22, IdDeferred);
    SendMessageW(ui.controls[IdDeferred], BM_SETCHECK,
                 s.deferred_recording ? BST_CHECKED : BST_UNCHECKED, 0);
    y += 26;
    Make(L"BUTTON", L"Classic PS4 frame pacing (fallback if timing looks wrong)",
         BS_AUTOCHECKBOX | WS_TABSTOP, C, y, W, 22, IdClassic);
    SendMessageW(ui.controls[IdClassic], BM_SETCHECK,
                 s.classic_vblank ? BST_CHECKED : BST_UNCHECKED, 0);
    y += 26;
    Make(L"BUTTON", L"Show this window at start (hold Shift to open it anyway)",
         BS_AUTOCHECKBOX | WS_TABSTOP, C, y, W, 22, IdShowLauncher);
    SendMessageW(ui.controls[IdShowLauncher], BM_SETCHECK,
                 s.show_launcher ? BST_CHECKED : BST_UNCHECKED, 0);
    y += 30;
    Label(L"Quit game: Left Ctrl + Left Shift + End (default shortcut).", C, y, W, 20);
    y += 38;

    Make(L"BUTTON", L"Play", BS_DEFPUSHBUTTON | WS_TABSTOP, C + W - 196, y, 96, 32, IdPlay);
    Make(L"BUTTON", L"Quit", BS_PUSHBUTTON | WS_TABSTOP, C + W - 92, y, 92, 32, IdQuit);
    ui.content_height = Px(y + 52);
}

void ScrollTo(int position) {
    RECT client;
    GetClientRect(g_ui->window, &client);
    const int next = std::clamp(
        position, 0, std::max(0, g_ui->content_height - static_cast<int>(client.bottom)));
    const int delta = g_ui->scroll - next;
    g_ui->scroll = next;
    ScrollWindowEx(g_ui->window, 0, delta, nullptr, nullptr, nullptr, nullptr,
                   SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
    SetScrollPos(g_ui->window, SB_VERT, next, TRUE);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_VSCROLL: {
        SCROLLINFO si{sizeof(si), SIF_ALL};
        GetScrollInfo(hwnd, SB_VERT, &si);
        int next = g_ui->scroll;
        switch (LOWORD(wp)) {
        case SB_LINEUP:
            next -= Px(36);
            break;
        case SB_LINEDOWN:
            next += Px(36);
            break;
        case SB_PAGEUP:
            next -= static_cast<int>(si.nPage);
            break;
        case SB_PAGEDOWN:
            next += static_cast<int>(si.nPage);
            break;
        case SB_THUMBTRACK:
            next = si.nTrackPos;
            break;
        case SB_TOP:
            next = 0;
            break;
        case SB_BOTTOM:
            next = si.nMax;
            break;
        }
        ScrollTo(next);
        return 0;
    }
    case WM_MOUSEWHEEL:
        ScrollTo(g_ui->scroll - GET_WHEEL_DELTA_WPARAM(wp) * Px(72) / WHEEL_DELTA);
        return 0;
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wp);
        const int id = GetDlgCtrlID(reinterpret_cast<HWND>(lp));
        SetBkMode(dc, TRANSPARENT);
        if (id == IdGameStatus) {
            SetTextColor(dc, g_ui->game_ok ? RGB(20, 120, 40) : RGB(170, 30, 30));
        } else if (id == IdFirmwareStatus) {
            SetTextColor(dc, g_ui->firmware_ok ? RGB(20, 120, 40) : RGB(170, 30, 30));
        }
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }
    case WM_PRINTCLIENT:
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = msg == WM_PAINT ? BeginPaint(hwnd, &ps) : reinterpret_cast<HDC>(wp);
        if (msg == WM_PRINTCLIENT) {
            RECT client;
            GetClientRect(hwnd, &client);
            FillRect(dc, &client, GetSysColorBrush(COLOR_WINDOW));
        }
        SetBkMode(dc, TRANSPARENT);
        HGDIOBJ old = SelectObject(dc, g_ui->title_font);
        RECT r{Px(24), Px(14) - g_ui->scroll, Px(600), Px(44) - g_ui->scroll};
        DrawTextW(dc, L"Driveclub-PC", -1, &r, DT_LEFT | DT_SINGLELINE);
        SelectObject(dc, g_ui->font);
        SetTextColor(dc, RGB(90, 90, 90));
        RECT r2{Px(24), Px(44) - g_ui->scroll, Px(580), Px(64) - g_ui->scroll};
        DrawTextW(dc, L"Tested: CUSA00093 v1.28. Other Driveclub regions are experimental.", -1,
                  &r2, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(dc, old);
        if (msg == WM_PAINT) {
            EndPaint(hwnd, &ps);
        }
        return 0;
    }
    case WM_COMMAND:
        // driveclub-pc: keyboard navigation must reveal controls below the visible area too.
        if (HIWORD(wp) == EN_SETFOCUS || HIWORD(wp) == BN_SETFOCUS || HIWORD(wp) == CBN_SETFOCUS) {
            RECT control;
            GetWindowRect(reinterpret_cast<HWND>(lp), &control);
            MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&control), 2);
            RECT client;
            GetClientRect(hwnd, &client);
            if (control.top < 0) {
                ScrollTo(g_ui->scroll + control.top - Px(8));
            } else if (control.bottom > client.bottom) {
                ScrollTo(g_ui->scroll + control.bottom - client.bottom + Px(8));
            }
        }
        switch (LOWORD(wp)) {
        case IdBrowse:
            if (const auto dir = PickFolder(hwnd, L"Driveclub base game folder (any region)")) {
                SetWindowTextW(g_ui->controls[IdGameEdit], dir->c_str());
            }
            return 0;
        case IdGameEdit:
            if (HIWORD(wp) == EN_CHANGE) {
                RefreshStatus();
            }
            return 0;
        case IdImport:
            ImportFirmware();
            return 0;
        case IdFirmwareDetails: {
            const auto title_id =
                ReadGameTitleId(TextPath(GetText(IdGameEdit))).value_or(std::string(TitleId));
            const auto report = InspectSysModules(title_id);
            MessageBoxW(hwnd, Widen(report.details).c_str(), L"Driveclub-PC firmware",
                        MB_OK | (report.ready ? MB_ICONINFORMATION : MB_ICONWARNING));
            return 0;
        }
        case IdSaveBrowse:
            if (const auto dir =
                    PickFolder(hwnd, L"Save data root (existing saves are not moved)")) {
                g_ui->settings.save_dir = Narrow(*dir);
                RefreshSavePath();
            }
            return 0;
        case IdSaveDefault:
            g_ui->settings.save_dir.clear();
            RefreshSavePath();
            return 0;
        case IdSaveOpen:
            OpenSaveFolder();
            return 0;
        case IdDisplay:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                UpdateWindowSizeEnabled();
            }
            return 0;
        case IdPlay:
            CollectSettings();
            {
                std::string error;
                if (!PrepareSaveRoot(g_ui->settings, error)) {
                    MessageBoxW(hwnd, Widen(error).c_str(), L"Driveclub-PC save data",
                                MB_OK | MB_ICONERROR);
                    return 0;
                }
            }
            g_ui->play = true;
            DestroyWindow(hwnd);
            return 0;
        case IdQuit:
            CollectSettings();
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        CollectSettings();
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/// Returns true when the user pressed Play; `s` holds the chosen settings either way.
bool ShowWindow(Settings& s) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    Ui ui;
    ui.settings = s;
    g_ui = &ui;
    ui.scale = static_cast<float>(GetDpiForSystem()) / 96.0f;

    NONCLIENTMETRICSW ncm{sizeof(ncm)};
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    LOGFONTW lf = ncm.lfMessageFont;
    lf.lfHeight = -Px(14);
    ui.font = CreateFontIndirectW(&lf);
    lf.lfHeight = -Px(24);
    lf.lfWeight = FW_SEMIBOLD;
    ui.title_font = CreateFontIndirectW(&lf);

    const HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); // IDC_ARROW
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    wc.lpszClassName = L"DriveclubPCLauncher";
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    RegisterClassExW(&wc);

    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    RECT rect{0, 0, Px(600), std::min(Px(920), static_cast<int>(work.bottom - work.top) - Px(60))};
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VSCROLL;
    AdjustWindowRectExForDpi(&rect, style, FALSE, 0, GetDpiForSystem());
    const int w = rect.right - rect.left, h = rect.bottom - rect.top;
    ui.window = CreateWindowExW(
        0, wc.lpszClassName, L"Driveclub-PC", style, (GetSystemMetrics(SM_CXSCREEN) - w) / 2,
        (GetSystemMetrics(SM_CYSCREEN) - h) / 2, w, h, nullptr, nullptr, inst, nullptr);
    BuildControls();
    RECT client;
    GetClientRect(ui.window, &client);
    SCROLLINFO si{sizeof(si),
                  SIF_RANGE | SIF_PAGE,
                  0,
                  ui.content_height - 1,
                  static_cast<UINT>(client.bottom),
                  0,
                  0};
    SetScrollInfo(ui.window, SB_VERT, &si, TRUE);
    RefreshStatus();
    ::ShowWindow(ui.window, SW_SHOW);
    SetForegroundWindow(ui.window);
    if (const char* snapshot = std::getenv("DCPC_LAUNCHER_SNAPSHOT")) {
        if (std::getenv("DCPC_LAUNCHER_SNAPSHOT_BOTTOM")) {
            ScrollTo(ui.content_height);
        }
        // Test hook: save an image of this window only (24-bit BMP) and close it.
        for (int i = 0; i < 40; ++i) {
            MSG pending;
            while (PeekMessageW(&pending, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&pending);
                DispatchMessageW(&pending);
            }
            Sleep(10);
        }
        RECT rc;
        GetWindowRect(ui.window, &rc);
        const int sw = rc.right - rc.left, sh = rc.bottom - rc.top;
        HDC screen = GetDC(nullptr);
        HDC mem = CreateCompatibleDC(screen);
        BITMAPINFO bi{};
        bi.bmiHeader = {sizeof(BITMAPINFOHEADER), sw, -sh, 1, 24, BI_RGB};
        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HGDIOBJ old = SelectObject(mem, bmp);
        SendMessageW(ui.window, WM_PRINT, reinterpret_cast<WPARAM>(mem),
                     PRF_NONCLIENT | PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
        const DWORD stride = static_cast<DWORD>((sw * 3 + 3) & ~3);
        BITMAPFILEHEADER fh{
            0x4D42,
            static_cast<DWORD>(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + stride * sh),
            0, 0, sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)};
        std::ofstream f(snapshot, std::ios::binary);
        f.write(reinterpret_cast<const char*>(&fh), sizeof(fh));
        f.write(reinterpret_cast<const char*>(&bi.bmiHeader), sizeof(bi.bmiHeader));
        f.write(static_cast<const char*>(bits), static_cast<std::streamsize>(stride) * sh);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
        DestroyWindow(ui.window);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(ui.window, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    s = ui.settings;
    DeleteObject(ui.font);
    DeleteObject(ui.title_font);
    UnregisterClassW(wc.lpszClassName, inst);
    g_ui = nullptr;
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
    return ui.play;
}

#endif

} // namespace

std::optional<fs::path> Run() {
    Settings s = Load();
    std::string error;
    const bool game_ok = !s.game_dir.empty() && ResolveAndValidateGame(TextPath(s.game_dir), error);
    bool firmware_ok = false;
    const auto title_id = ReadGameTitleId(TextPath(s.game_dir)).value_or(std::string(TitleId));
    FirmwareStatus(firmware_ok, title_id);
#ifdef _WIN32
    const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    if (s.show_launcher || shift || !game_ok || !firmware_ok) {
        const bool play = ShowWindow(s);
        Save(s);
        if (!play) {
            return std::nullopt;
        }
    }
#else
    if (!game_ok || !firmware_ok) {
        ShowError(error.empty() ? std::string("Set game_dir in user/driveclub_pc.ini") : error);
        return std::nullopt;
    }
#endif
    if (!PrepareSaveRoot(s, error)) {
        ShowError(error);
        return std::nullopt;
    }
    WriteGameConfig(s);
    ApplyRuntime(s);
    const auto out = TargetOutput(s);
    LOG_INFO(Loader, "Launcher: {} FPS, {} {}x{}, {}, sharpening {}, output {}x{}{}", s.target_fps,
             s.fullscreen ? "fullscreen" : "window", s.width, s.height,
             s.fsr ? "FSR 1" : "bilinear", s.sharpening, out.width, out.height,
             out.wide ? " (widescreen)" : "");
    return TextPath(s.game_dir);
}

bool ApplySavedRuntimeOptions(bool check_only) {
    std::error_code ec;
    if (fs::exists(IniPath(), ec)) {
        const auto settings = Load();
        std::string error;
        if (!PrepareSaveRoot(settings, error)) {
            if (check_only) {
                fmt::print(stderr, "{}\n", error);
            } else {
                ShowError(error);
            }
            return false;
        }
        ApplyRuntime(settings);
    }
    return true;
}

} // namespace DriveclubPC::Launcher
