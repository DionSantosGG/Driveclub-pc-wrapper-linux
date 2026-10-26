# Fixed tickrate experiment — 20.10.2026

Opt-in only: `DCPC_FIXED_TICKRATE=1`. Default timing is unchanged.
Based on kalaposfos' **60 FPS with fixed tickrate** v1.28 patch:
https://github.com/shadps4-emu/ps4_cheats/blob/main/PATCHES/Driveclub.xml

The upstream change at XML address `0x01762801` replaces the first character of
`fixedFramesPerSecond` with `g`. Our image-relative address is `0x1362801`.
Validate the whole NUL-terminated string and all three known deltatime sites
before changing anything. Restore known deltatime bytes to their original
values in memory, then apply this alternative. Unknown bytes skip the entire
alternative with an error; the deltatime path is not applied on that invocation.
`DCPC_FPS_UNLOCK=0` disables both patch paths.

This does not establish a replay-camera fix or support for 120 FPS. First test
at target 60, comparing race-clock speed and the same replay. The separate
manual launcher selects 60, 1440p and a private copy of the owner's test saves.
Game files, production dist and `gr2fork-dcpc-stage2-next` stay unchanged.

Owner's baseline feedback before this experiment: at 1080p/60 and 1440p/120,
audio sounded normal and no clear rendering errors were observed. The latter
included a demanding rainy race, third-person, first-person and cockpit views.
This is local manual feedback, not AMD validation or a controlled comparison
against 0.2.1. Replay shaking, including the race HUD, remains unresolved.

## Real elapsed-time experiment

Owner follow-up: fixed tickrate at target 60 was stable without replay shaking.
At target 120, actual ~80 FPS appeared ~25–30% faster, consistent with a fixed
60-Hz simulation step per rendered frame. This rules out shipping that mode
as a general 120-FPS solution. Rain is provisionally accepted, not PS4-verified.

`DCPC_FIXED_TICKRATE=1` plus `DCPC_FIXED_TICKRATE_REALTIME=1` tests the renamed
engine option with only the elapsed-time helper changed (`0x66306e`: `75 07`
to `90 90`). The fixed-fps check and flip interval are restored to their
original bytes. All existing signature checks run before any writes.
The new flag has no effect without the fixed-tickrate flag. Defaults unchanged.

Separate artifact: `build/gr2fork-dcpc-fixed-realtime`, private user and saves,
1080p, target 120, FPS counter enabled. Use `Start-Fixed-Realtime.cmd`;
the executable alone uses default deltatime. This is an experimental combination,
not an established speed or replay fix. Compare game-clock seconds against wall
time and repeat the replay test; neither host FPS nor a successful boot proves it.

## Selective main-clock experiment

Owner result: the global real-time helper restored normal speed at ~80 FPS,
but replay shaking returned. It is not a solution.

Read-only analysis of v1.28 shows `0x663040` is a shared clock update, not a
single game-speed switch. Its branch at `0x66306e` respects a clock's explicit
step (`object+0`) unless it equals the real-time sentinel. The global NOP patch
overrides every clock. The main manager initialises clocks +0x48/+0x88 to
16,666 us and also maintains derived clocks +0xc8/+0x108, including a separate
externally driven time path. These derived-clock paths are a candidate for the
replay regression; exact replay mode detection is not established.

`DCPC_FIXED_TICKRATE=1` + `DCPC_SELECTIVE_CLOCKS=1` leaves the shared helper
original and redirects only calls at `0x78ef3` and `0x78f2e` (+0x48/+0x88)
to a private real-time version. The 112-byte helper is cloned into mapped code
padding at `0x1552100`, separately from existing heap/widescreen stubs; its
three relative operands are relocated. Validate the full original helper,
two call sites, known deltatime sites and all padding before any writes.
Existing matching redirects/clone are accepted for idempotence.

No host callback, frame-rate divisor, external process patch or game-file write.
The selective flag takes precedence over FIXED_TICKRATE_REALTIME inside the
fixed-tickrate experiment; default timing remains unchanged.

Verification outside the runtime repo executes both the original guest helper
and its relocated clone against a deterministic clock: the original respects
16,666 us while the clone follows 12,500/10,000 us. Scaling, pause, original
real-time clocks, all 8 prior-patch mixtures, idempotence and atomic signature
rejections pass. These host-side checks do not prove actual replay correctness.
