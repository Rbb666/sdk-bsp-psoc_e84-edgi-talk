# SDLPal I2S Replay Handshake Fix

## Problem

On hardware, SDLPal opens `sound0` and loads `MUS.MKF`/`VOC.MKF`, but audio
stops after three writes. The observed RT-Thread state is deterministic:

- `pal_audi` is suspended on the exhausted `adu_mp` pool;
- both 512-byte replay blocks are allocated;
- `sound_th` is suspended on an empty `sound_tx` message queue;
- nobody is waiting on `sound_tx` semaphore;
- the I2S underrun counter remains zero.

The producer has queued RT-Audio data while the driver consumer is waiting for
a private message. The legacy playback task only asks RT-Audio for another
frame after receiving a previous private message, so this state has no wake-up
edge.

The first-frame path has a related defect: it writes a FIFO-sized zero block
directly but leaves `i2s_32_samples_frame_count` at zero and does not select the
prepared PCM buffer. Playback therefore depends on a later frame to bootstrap
the ISR state machine.

## Selected Design

Only the `BSP_USING_SDLPAL` branch in `libraries/HAL_Drivers/drv_i2s.c` changes.
Other projects retain the legacy path.

1. The first received frame becomes the active playback buffer. The task primes
   the FIFO through `i2s_write_32_samples()`, which also initializes the frame
   counter, before activating TDM.
2. After the hardware consumes a frame, the playback task calls
   `rt_audio_tx_complete()` unconditionally. RT-Audio already supplies a zeroed
   hardware frame when its replay queue is empty, so the driver must not poll
   the private `queue.is_empty` bitfield.
3. Existing 512-byte blocks and the two-block memory pool remain unchanged.
   Increasing buffers is explicitly outside this fix.
4. SDLPal-only counters expose transmitted messages, playback-task receives,
   TX FIFO interrupts, semaphore releases, and RT-Audio completion requests in
   `pal_audio`. These counters make a future stall attributable without another
   firmware instrumentation cycle.

## Error And Stop Behavior

The existing SDLPal stop/reset path remains responsible for disabling TDM and
resetting the message queue, semaphore, and RT-Audio queue. A reset-induced
message receive error continues directly to the next receive. The continuous
completion loop stops when RT-Audio invokes the driver stop callback and resets
the private queue.

## Testing

- Add a host contract test that fails while the SDLPal path polls
  `audio->replay->queue.is_empty` or primes the first frame with manual zeros.
- Verify the SDLPal first-frame path selects the active buffer and calls
  `i2s_write_32_samples()` before activation.
- Verify the completion call is unconditional in the SDLPal playback path.
- Run all existing C/C++ host tests and Python contract tests.
- Build the ARM target and run the ELF memory-layout checker.
- On hardware, wait at least five seconds in game and verify:
  `rendered` and `written` continue increasing, `rix ticks` becomes nonzero,
  `adu_mp` is not permanently exhausted, and music/effects are audible.

## Boundaries

- Do not modify `sdlpal/upstream`.
- Do not modify `vg_lite_hal.c`.
- Do not modify RT-Thread audio core sources.
- Do not change other projects' I2S behavior.
