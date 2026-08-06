# SDLPal Audio Diagnostics Removal Design

## Goal

Remove the temporary `pal_audio` shell command and its end-to-end diagnostics
plumbing now that board playback is working. Keep the validated audio playback
and lifecycle fixes unchanged.

## Removal Scope

- Delete `pal_audio_diagnostics.c` and `pal_audio_diagnostics.h`.
- Remove `pal_audio_diagnostics_get()` from the project audio contract.
- Remove audio diagnostic output from `pal_mem`.
- Remove `pal_audio_port_metrics_t`, its collection function, and the related
  counters from the audio port.
- Remove the SDLPal I2S metrics API, counters, and update sites from the shared
  I2S driver.
- Remove README references to the `pal_audio` command.
- Replace diagnostics-presence contract checks with diagnostics-absence checks.

## Retained Behavior

The following production behavior remains unchanged:

- DOS RIX music and VOC sound playback.
- Static audio buffers and the two-block RT-Audio replay pool.
- First-frame I2S priming and hardware-paced replay completion.
- Start/stop generation protection.
- Correct `rt_mq_recv()` message-length handling.
- Existing `BSP_USING_SDLPAL` protection for shared driver behavior.

## Verification

- A project contract test must fail while any removed shell or diagnostics
  symbol remains.
- All host C/C++ tests and Python tests must pass.
- The ARM target must compile and link.
- ELF memory validation must pass for rotation 90.
- `sdlpal/upstream` and `vg_lite_hal.c` must remain unchanged.
