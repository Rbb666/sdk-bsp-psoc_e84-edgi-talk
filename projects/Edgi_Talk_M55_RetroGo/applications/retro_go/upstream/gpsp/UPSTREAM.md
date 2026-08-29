# gpSP snapshot for GBA support

This directory is an unmodified snapshot of `components/gpsp` from
[`Irak4t0n/HowBoyAdvance`](https://github.com/Irak4t0n/HowBoyAdvance) commit
`d2ed7442e7fbca96146ce043ae127835fa7e4ce8`. That project is the source of the
ESP32-P4 gpSP work referenced by retro-go issue #349.

The PSoC Edge build deliberately excludes `riscv/riscv_stub.S`,
`riscv/riscv_emit.h`, `gpsp_esp.c`, and the other ESP-only integration files.
The Cortex-M55 cannot execute the ESP32-P4 RISC-V dynarec output, so the local
platform adapter uses gpSP's portable interpreter while retaining its paged
ROM cache, video, sound, save-memory, and HLE/OpenGBA BIOS support.

gpSP is licensed under GPL-2.0-or-later; the original copyright and license
headers are retained in every upstream source file. The project's GPL-2.0
license text is also available in the neighboring gnuboy snapshot's
`COPYING` file.
