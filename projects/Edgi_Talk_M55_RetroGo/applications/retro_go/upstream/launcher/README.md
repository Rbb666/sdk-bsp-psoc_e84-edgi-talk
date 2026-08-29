# Official retro-go launcher resources

The generated launcher assets in `platform/retro_go_launcher_assets.c` come
from `themes/default` at retro-go commit
`4ced120669750ca7228fd0414211430c1d923166`. The PSoC menu follows the cold-boot
carousel and browser layout in `launcher/main/gui.c` while replacing the
`rg_system` display, storage and input services with the local platform layer.

`platform/retro_go_launcher_font.c` is generated from the official
`components/retro-go/fonts/VeraBold11.c`. The complete Bitstream Vera
copyright and permission notice is retained in `FONT_COPYRIGHT.txt`.

The launcher is licensed under GPL-2.0. Its `launcher/main/COPYING` file is
retained as `COPYING`; the original `CREDITS` is retained alongside it. The
default GB/GBC artwork is derived from the GBZ35 EmulationStation theme and its
complete attribution and CC BY-NC-SA 3.0 notice is retained in
`THEME_SOURCE_README.md`.

Regenerate the packed RGB565 resources from an upstream checkout with:

```powershell
python tools/generate_retro_go_launcher_assets.py
```
