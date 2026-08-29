# Retro-Go PSoC Edge platform

This directory contains the PSoC Edge/RT-Thread platform layer. The emulator
core under `upstream/gnuboy` is an unmodified snapshot copied from retro-go
commit `4ced1206` (`retro-core/components/gnuboy`).

Game Boy and Game Boy Color use the unchanged retro-go gnuboy snapshot. GBA
uses an unchanged gpSP snapshot from `HowBoyAdvance@d2ed7442`, the codebase
referenced by retro-go issue #349. The ESP32-P4 RV32 dynarec and legacy A32
backend cannot execute on Thumb-only Cortex-M55, so this build selects gpSP's
portable `cpu.cc` interpreter, a 6 MiB paged ROM cache, 32 kHz audio generation,
and true render skipping. Supported extensions are `.gb`, `.gbc`, and `.gba`.

All hardware access remains in `platform/` and uses public RT-Thread device,
CherryUSB Host HID and VG-Lite APIs. The vendor LCD, I2S and USB sources are not
modified by this integration. The standalone build selects the vendor I2S
driver's existing primed 16 ms playback branch for `drv_i2s.c` only. Audio and
USB workers preempt the emulator while blocked normally, and adaptive display
frame skipping keeps CPU/audio emulation near the Game Boy's 59.7275 Hz rate.

The startup and ROM-selection screens adapt the official retro-go launcher
carousel/browser layout, GB/GBC/GBA theme artwork, and VeraBold11 font. Its native
320x240 canvas is integer-scaled to 640x480; no LVGL code is linked. MSH remains
at priority 20 while all Retro-Go threads use priorities 21 through 25. See
`upstream/launcher` for GPL-2.0, font, credits, and CC BY-NC-SA 3.0 notices.
