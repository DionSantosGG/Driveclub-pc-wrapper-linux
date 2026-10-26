# Driveclub launcher changes — 20.10.2026

Based on the 0.2.1 runtime (`b22594e`). No FSR 3, frame generation, shader autopatcher,
photo-mode, audio, camera or FOV changes are included.

## Saves

The launcher offers **Save data root**, **Browse**, **Open folder** and **Use default**.
An empty `save_dir` in `user/driveclub_pc.ini` preserves the existing location, normally
`user/home`. A custom absolute path redirects save data only. Settings, firmware,
trophies and user profiles keep their existing paths.

The save hierarchy is `<root>/<user-id>/savedata/<title-id>/<save-slot>`; the usual user
ID is `1000`. Open folder opens the selected root. No saves are automatically copied,
moved, merged or deleted. To keep existing progress, copy the corresponding hierarchy
from the old root to the new one while the game is closed. Do not rename regional
save directories to transfer progress: cross-region save compatibility is unverified.

The launcher and command-line launch both apply `save_dir`. A custom path must be
absolute, a directory, and writable. Failure stops launch instead of falling back to a
different save directory. Save-memory, mounting, save search, backup and mount-info
paths use the same root.

## Game regions

The launcher accepts CUSA00093, CUSA00003 and CUSA00879. Other CUSA IDs are accepted
when the SFO title identifies a Driveclub base game (not standalone Bikes). Only
**CUSA00093 v1.28** was tested locally; every other region is marked experimental.
Each region uses its own config (`user/custom_configs/<title-id>.json`) and existing
save namespace. An adjacent update with a different or unreadable title ID is rejected.
The existing engine/60-FPS/widescreen patches retain their instruction-byte guards;
accepting a region does not guarantee that these patches match its executable.

## Firmware

Required files are `libSceLibcInternal.sprx` (the existing launcher requirement),
`libSceJpegDec.sprx`, `libSceFreeTypeOt.sprx` and `libSceAudiodec.sprx`. The latter three
were loaded in successful v1.28 runs and have no HLE registration in the runtime's
allowed-LLE table.

These observed modules have an HLE fallback and are optional: `libSceRtc.sprx`,
`libScePngDec.sprx`, `libSceFont.sprx`, `libSceFontFt.sprx`, `libSceAvPlayer.sprx`.
The four-module setup reached and continued a race in the local functional check;
this does not prove equal behaviour of every fallback in every event or video.

**Details** lists missing required files, absent optional files, and unreadable or
invalid basic ELF/SELF headers. It checks the selected region's firmware overrides
before global modules, except libc, which the linker loads globally. It does not
authenticate firmware versions, validate every segment or prove successful decryption.
Use decrypted modules from your own console. Import keeps existing files and reports
copied/skipped/failed counts; replacing an invalid existing file is a manual operation.

## Quit shortcut and diagnostics

The launcher displays the existing default **Left Ctrl + Left Shift + End** shortcut.
It opens the existing confirmation overlay; no new in-game controls were added.

```powershell
.\Driveclub-PC.exe --dcpc-check -g 'D:\Games\Driveclub'
```

This checks game metadata, firmware and the configured save root, prints diagnostics
and exits without booting the game or showing error dialogs. Exit 0 means these
preflight checks passed; it does not mean regional gameplay was tested. As with normal
startup, portable settings/user directories may be initialized, and a configured custom
save root is created and checked for write access with a temporary empty directory.

The expanded Windows launcher scrolls on smaller displays. Linux launcher integration
is planned separately with PR #18.
