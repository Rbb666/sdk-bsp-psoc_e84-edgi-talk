# SDLPal DOS Audio Integration Design

Date: 2026-08-06

Status: Approved in conversation, pending written-spec review

## 1. Goal

Add DOS-version SDLPal audio to `projects/Edgi_Talk_M55_SDLPAL` on PSoC Edge M55. The first usable milestone plays RIX background music from `/sdcard/pal/mus.mkf`; the second adds up to four simultaneous VOC sound effects from `/sdcard/pal/voc.mkf` and mixes them with the music.

Audio is output through the existing RT-Thread `sound0` device implemented by `libraries/HAL_Drivers/drv_i2s.c`. The target format is 16 kHz, signed 16-bit, mono. The existing I2S driver duplicates mono samples to the codec's stereo transport.

## 2. Non-goals and source boundaries

- Do not modify `projects/Edgi_Talk_M55_SDLPAL/sdlpal/upstream`.
- Do not modify `vg_lite_hal.c`.
- Keep audio implementation and imported decoder code private to `projects/Edgi_Talk_M55_SDLPAL` whenever possible.
- Any unavoidable change to common code must be guarded by `BSP_USING_SDLPAL`, with unchanged behavior for every other project.
- Do not add SDL, SDL_mixer, LVGL, MP3, OGG, OPUS, native MIDI, or desktop audio backends.
- Do not store decoded PCM copies of the complete resource set.

The RIX decoder and fixed-memory OPL2 implementation are derived from `origin/extreme` in `D:\workspace_rb\OpenSouce\sdlpal-embedded`, commit `23177627e619731188591288215dc2a61d884ae7`. Imported files must retain their original copyright and license headers, and the project provenance document must record the exact source paths and commit.

## 3. Chosen architecture

Use a platform-native, fixed-memory audio pipeline instead of importing SDLPal's desktop audio stack. The desktop stack includes large floating-point resampler tables and more dynamic allocation than this target needs.

The implementation has four project-private layers:

1. **SDLPal audio contract** implements the existing `AUDIO_*` API declared by upstream `audio.h`. It replaces `unix/contract_noaudio.c` in this project build, so upstream call sites remain unchanged.
2. **Resource/cache layer** reads and validates individual MKF chunks on the game thread, stores immutable raw RIX/VOC data in a bounded HyperRAM LRU cache, and publishes reference-counted handles.
3. **Mixer core** is host-testable and independent of RT-Thread. It renders RIX through the fixed-memory OPL2 emulator, decodes/resamples VOC effects, applies music/SFX volume and fades, mixes four voices, and saturates the result to PCM16.
4. **RT-Thread audio port** owns a static producer thread, opens `sound0`, renders fixed-duration blocks, and writes them to the RT-Thread audio device.

The data flow is:

`SDLPal AUDIO_* call -> MKF lookup/cache on game thread -> fixed command queue -> mixer thread -> 256-sample PCM16 block -> sound0 -> drv_i2s -> ES8388`

No SDLPal engine-core source change is needed.

## 4. Audio timing and format

- Output rate: 16,000 samples/second.
- Output format: signed 16-bit mono.
- Producer block: 256 samples, 512 bytes, 16 ms.
- Maximum active SFX voices: four.
- RIX timing: preserve the DOS 70 Hz music tick with an integer phase accumulator. Since 16,000 is not divisible by 70, tick boundaries alternate at the required sample positions without cumulative drift.
- VOC resampling: fixed-point linear interpolation into the 16 kHz output domain. No floating-point lookup tables are used.
- Final mix: accumulate in at least 32 bits, then saturate once to signed PCM16.

The VOC parser supports the block types required by the DOS resource set, including sound-data, continuation, silence, and extended-format metadata where present. Malformed sizes, invalid sample rates, unsupported codecs, and truncated blocks are rejected without affecting the game.

## 5. Threading and ownership

`AUDIO_PlayMusic`, `AUDIO_PlaySound`, and volume-control calls originate on the SDLPal game thread. File I/O, MKF parsing, HyperRAM allocation, cache eviction, and resource destruction are restricted to that thread.

The audio producer thread may only:

- consume fixed-size commands;
- advance RIX/OPL and VOC voice state;
- render and mix a PCM block;
- call the already-open `sound0` device;
- update lock-free or briefly protected telemetry counters.

It must not allocate, free, open files, read the SD card, or print logs.

Commands carry generation numbers so stale music transitions cannot override newer requests. A full SFX queue drops the new SFX request and increments a counter. Music commands are coalesced so the latest requested state wins. If all four SFX voices are active, a new valid effect replaces the oldest voice.

Cached resource buffers are immutable. Reference counts prevent eviction of current, pending, or active resources. Cache metadata updates use short RT-Thread critical sections; no lock is held while rendering, performing SD I/O, or writing audio.

## 6. Lifecycle

`AUDIO_OpenDevice` initializes static state, locates `sound0`, configures 16 kHz/16-bit playback, opens the device, initializes the cache and OPL state, and starts the static audio thread. Until a valid command arrives, the thread outputs silence.

The normal upstream path ignores the return value of `AUDIO_OpenDevice`, so an open/configuration failure disables audio and logs one diagnostic while allowing the game to continue.

`AUDIO_PlayMusic` validates and caches the requested `mus.mkf` chunk before publishing a loop/fade/switch command. `AUDIO_PlaySound` validates and caches the requested `voc.mkf` chunk before publishing a voice-start command. Music stop, loop, volume, fade-in, and fade-out semantics follow the existing `AUDIO_*` contract.

`AUDIO_CloseDevice` asks the producer thread to stop, waits for a bounded acknowledgement, closes `sound0`, and then releases cached data from the game thread. If the thread fails to stop within the bound, live buffers are retained rather than freed, preventing a use-after-free during shutdown.

## 7. Memory placement and budgets

Hot mutable state belongs in on-chip Secondary SRAM:

- static audio thread stack: initially 8 KiB;
- two PCM blocks: 1 KiB total;
- command queue and synchronization objects: approximately 1 KiB;
- four voice states and fixed-point resampler state: less than 1 KiB;
- cache metadata and RIX/OPL mutable state: remaining project audio budget.

The audio feature's total additional on-chip SRAM budget is 48 KiB, including its static thread stack but excluding buffers already owned by the existing audio driver. The final map file and runtime stack high-water data must verify this. If measured stack use remains below 50%, the configured stack may be reduced later; the acceptance ceiling is 70% use.

The fixed OPL2 lookup data, currently about 25,706 bytes, is `const` and remains in Flash. Raw RIX and VOC chunks are cold data and use a HyperRAM LRU cache with a hard 1 MiB cap. The cache evicts only zero-reference entries. It does not put mixer state, PCM work buffers, thread stacks, or per-sample lookup data in HyperRAM.

No audio allocation uses the GFX region. That region remains available for display buffers and the existing SDLPal allocation fallback, where battle scenes already create peak pressure.

Under `BSP_USING_SDLPAL` only, the common I2S driver's playback block/FIFO constants may be reduced to match the 256-sample producer block and lower latency. The original constants remain the default for all other projects.

## 8. Failure handling

- Missing or corrupt `mus.mkf` track: reject the request; retain the current valid track when possible, otherwise play silence.
- Missing or corrupt `voc.mkf` effect: drop only that effect.
- HyperRAM cache full: evict inactive least-recently-used entries; if none are eligible, drop the request.
- SFX command queue full: drop the new effect.
- Music command queue full: coalesce to the latest desired music state.
- `sound0` open/configure failure: disable audio for the session; continue the game.
- Audio write failure or short write: count it, recover with silence, and avoid replaying stale PCM.
- Render underrun: zero-fill the missing part of the block and count it.

Repeated failures are counted but not logged from the audio thread. User-visible gameplay must not terminate solely because audio is unavailable.

## 9. Diagnostics

Extend project diagnostics with an audio report, either as `pal_audio` or a clearly separated section of `pal_mem`. It reports:

- device/open state and disabled reason;
- rendered blocks and non-silent blocks;
- current and peak active SFX voices;
- render and device-write last/max time;
- underruns, short writes, and write failures;
- command drops/coalesces;
- cache current/peak bytes, hits, misses, evictions, and allocation failures;
- audio thread stack current/peak use and total size.

Counters are fixed-width and updated without printing from the real-time path.

## 10. Implementation stages

### Stage 1: RIX background music

- Enable the project audio configuration and build the project-private audio contract.
- Integrate `sound0` at 16 kHz mono with a static producer thread.
- Import/adapt the locked RIX and fixed-memory OPL2 implementation.
- Implement MKF music cache, looping, stop, volume, and fades.
- Validate map, menu, scene-transition, and battle music changes.

### Stage 2: VOC sound effects

- Implement bounds-checked DOS VOC parsing.
- Implement fixed-point 16 kHz resampling and four-voice mixing.
- Add SFX volume, oldest-voice replacement, and cache references.
- Validate menu, confirmation, attack, spell, and battle effects while music plays.

## 11. Test strategy

Development follows test-first changes. Host tests cover:

- MKF chunk bounds, empty chunks, malformed offsets, and truncated files;
- RIX command/lifecycle state and exact 70 Hz phase accumulation;
- VOC block parsing, invalid rates/codecs, continuation, silence, and truncation;
- fixed-point resampling at representative DOS VOC rates;
- one-to-four voice mixing, music plus SFX, volume, fades, and PCM16 saturation;
- command ordering, stale-generation rejection, queue-full behavior, and voice replacement;
- LRU eviction, reference protection, 1 MiB cap, and allocation failure;
- device-open/write failures and silence recovery;
- build contracts that prohibit edits under `sdlpal/upstream`, keep common changes macro-guarded, and leave `vg_lite_hal.c` untouched;
- map/ELF budget checks for Flash, on-chip SRAM, HyperRAM policy, and thread stack.

Target verification covers:

- cold boot with valid resources and with each audio resource missing;
- map exploration and repeated scene/music transitions;
- battle entry/exit and four simultaneous effects;
- save/load while music and effects are active;
- touch and display responsiveness compared with the no-audio build;
- at least ten minutes of continuous play with diagnostics captured.

## 12. Acceptance criteria

- Game startup and loading never fail solely because audio initialization or a sound resource fails.
- DOS RIX music plays, loops, stops, fades, and switches at the correct game events.
- DOS VOC effects play with music, with up to four simultaneous voices.
- Audible control-effect latency is no more than one 16 ms producer block after the resource is cached.
- Ten minutes of representative play produces no audio-thread stack overflow, system hang, or reported underrun.
- Maximum block render time stays below 16 ms under music plus four effects.
- Audio HyperRAM use stays at or below 1 MiB.
- Additional project audio SRAM stays at or below 48 KiB, and audio-thread peak stack use stays below 70% of 8 KiB.
- Display frame pacing, touch response, battle allocation, and save/load behavior show no material regression.
- `sdlpal/upstream` and `vg_lite_hal.c` remain unchanged.
- Other projects build with their original I2S settings and behavior.
