# Stage 2 follow-up: depth, audio, camera — 20.10.2026

Continues `f4ddd88`, based on 0.2.1 + stage 1. Release+LTO test build, no new
PGO and no FSR/FG experiments. Production distribution and its user data stay
unchanged. No issue has been closed or external comment posted.

## Depth preservation

The previously skipped D32Sfloat four-sample -> RGBA32F single-sample alias
conversion now renders each depth sample into its corresponding RGBA channel.
This is the inverse of the existing channel-to-sample depth path, not an
averaged resolve. It reuses the blit helper's descriptor, pipeline and dynamic
state invalidation, with explicit source/destination image transitions and
deferred image-view destruction.

The path requires equal 2D extents, one level/layer and hardware support for
four samples. Other conversions keep their existing warning. A standalone
Vulkan test using the actual generated host shaders verifies 48 float values,
including 0 and 1, survive RGBA -> depth4 -> RGBA bit-exactly on the local
RTX 5070 Ti Laptop. Gameplay tests reach races without the previous missing-copy
warning. AMD visual correctness still requires AMD hardware.

`DCPC_MS_DEPTH_COPY=0` restores the diagnostic legacy path; default enabled.

## Audio packet delivery

Targets [audio issue #11](https://github.com/ondrejeder/Driveclub-pc-wrapper/issues/11).
The game uses 256-frame 48 kHz eight-channel blocks (5.33 ms). The old worker
polled once per period: a packet arriving just after the check lost a whole
delivery period. It now waits for actual packet arrival, while retaining the
sample clock. First output and idle periods over 100 ms reset deadline debt.
Close/stop wake the worker; buffer ownership remains protected by the port mutex.

OpenAL now accumulates approximately 32 ms of real PCM before starting or
recovering, rather than repeatedly starting a single short block. Stopped
sources are rewound after processed buffers are reclaimed, so the recovery
queue can accumulate. The queue has spare buffers; if temporarily full, it
waits for a processed buffer rather than silently dropping PCM. A 100 ms
failure to drain is logged as an error. No pitch or resampling-speed change.

SDL starts after a similar short prefill. Queue sizes use the actual stream
channel count after stereo conversion. [SDL streams start paused](https://wiki.libsdl.org/SDL3/SDL_OpenAudioDeviceStream);
the prefill precedes resuming playback. [Queued input bytes](https://wiki.libsdl.org/SDL3/SDL_GetAudioStreamQueued)
are not a direct measure of hardware underruns. Channel mix coefficients and
decoders are unchanged.

- `DCPC_AUDIO_BUFFERING=0`: restore legacy polling/start/recovery for diagnosis.
- `DCPC_AUDIO_STATS=1`: five-second packet cadence, queue, restart/drop and
  finite/peak sample observations. No audio is recorded; default off.

Standalone host tests compile the actual worker loop against a recording sink:
idle stop completes, 40 packets remain ordered/intact, and a long idle does not
cause an unpaced catch-up burst. This verifies host synchronization, not PS4 API
semantics or listening quality.

OpenAL 30 FPS, common 65–90 s interval, two audio ports / about 50 port-seconds:
**292 restarts -> 0**, no dropped blocks/nonfinite samples. Before: about
47,431–47,898 frames/s; after: 47,991–48,001 frames/s. The complete corrected
run records only its three initial port starts. SDL 60 FPS also completes with
no queue resets/drops/nonfinite samples. These are local stereo measurements;
the complete audible Shadow PC symptom is not confirmed fixed remotely.

## Camera investigation

[Issue #7](https://github.com/ondrejeder/Driveclub-pc-wrapper/issues/7) concerns
replays at 60 FPS. The owner's loading-camera report may be a different case.
Captured the event preview at 30/60 FPS, then compared 480 Hz vs 60 Hz vblank
while retaining the independent 60 FPS flip thread. No convincing improvement
was established. The experimental vblank override was removed; default timing
and game camera/gyro behavior are unchanged.

Captures perturb pacing, the camera paths are not aligned and the short 60 Hz
run stopped while a PNG was being written (24 complete preview captures).
The available save copy contains no actual saved replay to replay identically.
Camera shake is unresolved; no guessed camera patch is enabled.

Full run IDs, diagnostic scripts and limitations:
`../docs/stage2-audio-camera-20261020.md`.
