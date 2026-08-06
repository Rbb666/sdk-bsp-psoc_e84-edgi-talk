#!/usr/bin/env python3
"""Validate the linked SDLPal image against its memory and feature contract."""

from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys
from typing import Dict, Iterable, List, Tuple


PAL_FRAMEBUFFER_BYTES = 128 * 1024
PAL_THREAD_STACK_BYTES = 24 * 1024
PAL_AUDIO_MAX_BYTES = 48 * 1024
M55_ITCM_RESERVED_BYTES = 64 * 1024
LCD_INDEXED_STAGING_BYTES = 320 * 200
PAL_LARGE_MIN_BYTES = 593362
PAL_LARGE_MAX_BYTES = 0x93000
PAL_SAVE_RESERVE_BYTES = 192 * 1024
PAL_RESOURCE_POOL_BYTES = 128 * 1024
VALID_ROTATIONS = (0, 90, 180, 270)
ITCM_HOT_SYMBOLS = (
    "PAL_GameMain",
    "PAL_StartFrame",
    "PAL_MakeScene",
    "PAL_MapBlitToSurface",
    "PAL_RLEBlitToSurface",
    "PAL_InterpretInstruction",
    "PAL_BattleStartFrame",
    "SDL_UpperBlit",
)


def parse_nm_output(output: str) -> Dict[str, int]:
    symbols: Dict[str, int] = {}
    pattern = re.compile(
        r"^(?P<address>[0-9a-fA-F]+)(?:\s+[0-9a-fA-F]+)?"
        r"\s+[A-Za-z]\s+(?P<name>\S+)$"
    )
    for line in output.splitlines():
        match = pattern.match(line.strip())
        if match:
            symbols[match.group("name")] = int(match.group("address"), 16)
    return symbols


def parse_map(text: str) -> Tuple[Dict[str, Tuple[int, int]], Dict[str, Tuple[int, int]]]:
    regions: Dict[str, Tuple[int, int]] = {}
    sections: Dict[str, Tuple[int, int]] = {}
    region_pattern = re.compile(
        r"^(?P<name>[A-Za-z0-9_*]+)\s+"
        r"0x(?P<origin>[0-9a-fA-F]+)\s+0x(?P<length>[0-9a-fA-F]+)"
    )
    section_pattern = re.compile(
        r"^\.(?P<name>pal_framebuffer|sdlpal_thread|sdlpal_audio|cy_gpu_buf)\s+"
        r"0x(?P<origin>[0-9a-fA-F]+)\s+0x(?P<length>[0-9a-fA-F]+)"
    )
    section_values_pattern = re.compile(
        r"^0x(?P<origin>[0-9a-fA-F]+)\s+0x(?P<length>[0-9a-fA-F]+)"
    )
    in_memory_configuration = False
    pending_section = None

    for raw_line in text.splitlines():
        line = raw_line.strip()
        if line == "Memory Configuration":
            in_memory_configuration = True
            continue
        if line == "Linker script and memory map":
            in_memory_configuration = False
            continue
        if line in (
            ".pal_framebuffer",
            ".sdlpal_thread",
            ".sdlpal_audio",
            ".cy_gpu_buf",
        ):
            pending_section = line[1:]
            continue
        if pending_section is not None:
            match = section_values_pattern.match(line)
            if match:
                sections.setdefault(
                    pending_section,
                    (
                        int(match.group("origin"), 16),
                        int(match.group("length"), 16),
                    ),
                )
            pending_section = None
        if in_memory_configuration:
            match = region_pattern.match(line)
            if match and match.group("name") != "Name":
                regions[match.group("name")] = (
                    int(match.group("origin"), 16),
                    int(match.group("length"), 16),
                )
        match = section_pattern.match(line)
        if match:
            sections.setdefault(
                match.group("name"),
                (
                    int(match.group("origin"), 16),
                    int(match.group("length"), 16),
                ),
            )
    return regions, sections


def _forbidden_symbol_errors(names: Iterable[str]) -> List[str]:
    errors: List[str] = []
    for name in sorted(names):
        lower = name.lower()
        if re.search(r"(^|_)lv_", lower) or lower == "lvgl_thread_init":
            errors.append(f"LVGL symbol linked: {name}")
    return errors


def validate_layout(
    symbols: Dict[str, int],
    regions: Dict[str, Tuple[int, int]],
    rotation: int,
) -> List[str]:
    errors = _forbidden_symbol_errors(symbols)
    required = (
        "__pal_framebuffer_start__",
        "__pal_framebuffer_end__",
        "__bss_end__",
        "__StackLimit",
        "__sdlpal_thread_start__",
        "__sdlpal_thread_end__",
        "__sdlpal_audio_start__",
        "__sdlpal_audio_end__",
        "__HeapBase",
        "__cy_gpu_buf_start__",
        "__cy_gpu_buf_end__",
        "__lcd_indexed_staging_start__",
        "__lcd_indexed_staging_end__",
        "__sdlpal_large_start__",
        "__sdlpal_large_end__",
        "__sdlpal_save_start__",
        "__sdlpal_save_end__",
        "__sdlpal_resource_start__",
        "__sdlpal_resource_end__",
        "__sdlpal_itcm_start__",
        "__sdlpal_itcm_end__",
        "__ram_vectors_end__",
    )
    for name in required:
        if name not in symbols:
            errors.append(f"missing linker symbol: {name}")

    if rotation not in VALID_ROTATIONS:
        errors.append(f"invalid rotation: {rotation}")

    itcm = regions.get("m55_code_INTERNAL")
    if itcm is None:
        errors.append("missing m55_code_INTERNAL map region")
    else:
        origin, length = itcm
        limit = origin + length
        budget_end = limit - M55_ITCM_RESERVED_BYTES
        start = symbols.get("__sdlpal_itcm_start__")
        end = symbols.get("__sdlpal_itcm_end__")
        vectors_end = symbols.get("__ram_vectors_end__")

        if start is not None and end is not None:
            if not (origin <= start < end <= budget_end):
                errors.append(
                    "SDLPal ITCM range is invalid or exceeds its budget"
                )
        if vectors_end is not None and vectors_end > budget_end:
            errors.append("ITCM layout does not preserve the required 64 KiB")

        for name in ITCM_HOT_SYMBOLS:
            address = symbols.get(name)
            if address is None:
                errors.append(f"missing ITCM hot symbol: {name}")
            elif start is not None and end is not None:
                if not (start <= address < end):
                    errors.append(
                        f"ITCM hot symbol is outside ITCM SDLPal range: {name}"
                    )
            elif not (origin <= address < limit):
                errors.append(f"ITCM hot symbol is outside ITCM: {name}")

    if all(name in symbols for name in required[:2]):
        framebuffer_size = (
            symbols["__pal_framebuffer_end__"]
            - symbols["__pal_framebuffer_start__"]
        )
        if framebuffer_size != PAL_FRAMEBUFFER_BYTES:
            errors.append(
                f"framebuffer must be 131072 bytes, got {framebuffer_size}"
            )

    dtcm = regions.get("m55_data_INTERNAL")
    if dtcm is None:
        errors.append("missing m55_data_INTERNAL map region")
    elif all(
        name in symbols
        for name in (
            "__pal_framebuffer_start__",
            "__pal_framebuffer_end__",
            "__bss_end__",
            "__StackLimit",
        )
    ):
        origin, length = dtcm
        limit = origin + length
        if symbols["__pal_framebuffer_start__"] < origin:
            errors.append("DTCM framebuffer starts below its region")
        if symbols["__pal_framebuffer_end__"] > limit:
            errors.append("DTCM framebuffer overflows its region")
        if symbols["__bss_end__"] > symbols["__StackLimit"]:
            errors.append("DTCM bss overlaps the main stack")
        if symbols["__StackLimit"] > limit:
            errors.append("DTCM stack limit exceeds its region")

    gfx = regions.get("gfx_mem")
    if gfx is None:
        errors.append("missing gfx_mem map region")
    elif all(
        name in symbols
        for name in ("__cy_gpu_buf_start__", "__cy_gpu_buf_end__")
    ):
        origin, length = gfx
        if symbols["__cy_gpu_buf_start__"] < origin:
            errors.append("GFX section starts below its region")
        if symbols["__cy_gpu_buf_end__"] > origin + length:
            errors.append("GFX section overflows its region")

        staging_names = (
            "__lcd_indexed_staging_start__",
            "__lcd_indexed_staging_end__",
        )
        if all(name in symbols for name in staging_names):
            staging_start = symbols[staging_names[0]]
            staging_end = symbols[staging_names[1]]
            staging_bytes = staging_end - staging_start
            if staging_bytes != LCD_INDEXED_STAGING_BYTES:
                errors.append(
                    "VG-Lite indexed staging must be "
                    f"{LCD_INDEXED_STAGING_BYTES} bytes, got {staging_bytes}"
                )
            if staging_start < origin or staging_end > origin + length:
                errors.append("VG-Lite indexed staging is outside GFX memory")

        large_names = ("__sdlpal_large_start__", "__sdlpal_large_end__")
        if all(name in symbols for name in large_names):
            large_start = symbols[large_names[0]]
            large_end = symbols[large_names[1]]
            large_bytes = large_end - large_start
            if large_bytes < PAL_LARGE_MIN_BYTES:
                errors.append(
                    "PAL_LARGE GFX storage must be at least "
                    f"{PAL_LARGE_MIN_BYTES} bytes, got {large_bytes}"
                )
            if large_bytes > PAL_LARGE_MAX_BYTES:
                errors.append(
                    "PAL_LARGE GFX storage exceeds "
                    f"{PAL_LARGE_MAX_BYTES} bytes, got {large_bytes}"
                )
            if (
                large_start < symbols["__cy_gpu_buf_start__"]
                or large_end > symbols["__cy_gpu_buf_end__"]
            ):
                errors.append("PAL_LARGE storage is outside the GFX section")

        save_names = ("__sdlpal_save_start__", "__sdlpal_save_end__")
        if all(name in symbols for name in save_names):
            save_start = symbols[save_names[0]]
            save_end = symbols[save_names[1]]
            save_bytes = save_end - save_start
            if save_bytes != PAL_SAVE_RESERVE_BYTES:
                errors.append(
                    "SDLPal save reserve must be "
                    f"{PAL_SAVE_RESERVE_BYTES} bytes, got {save_bytes}"
                )
            if (
                save_start < origin
                or save_end > origin + length
                or save_start < symbols["__cy_gpu_buf_start__"]
                or save_end > symbols["__cy_gpu_buf_end__"]
            ):
                errors.append("SDLPal save reserve is outside GFX memory")

        resource_names = (
            "__sdlpal_resource_start__",
            "__sdlpal_resource_end__",
        )
        if all(name in symbols for name in resource_names):
            resource_start = symbols[resource_names[0]]
            resource_end = symbols[resource_names[1]]
            resource_bytes = resource_end - resource_start
            if resource_bytes != PAL_RESOURCE_POOL_BYTES:
                errors.append(
                    "SDLPal engine resource pool must be "
                    f"{PAL_RESOURCE_POOL_BYTES} bytes, got {resource_bytes}"
                )
            if (
                resource_start < origin
                or resource_end > origin + length
                or resource_start < symbols["__cy_gpu_buf_start__"]
                or resource_end > symbols["__cy_gpu_buf_end__"]
            ):
                errors.append("SDLPal engine resource pool is outside GFX memory")

    secondary = regions.get("m55_data_secondary")
    if secondary is None:
        errors.append("missing m55_data_secondary map region")
    elif all(
        name in symbols
        for name in (
            "__sdlpal_thread_start__",
            "__sdlpal_thread_end__",
            "__sdlpal_audio_start__",
            "__sdlpal_audio_end__",
            "__HeapBase",
        )
    ):
        origin, length = secondary
        thread_start = symbols["__sdlpal_thread_start__"]
        thread_end = symbols["__sdlpal_thread_end__"]
        audio_start = symbols["__sdlpal_audio_start__"]
        audio_end = symbols["__sdlpal_audio_end__"]
        if thread_start < origin or thread_end > origin + length:
            errors.append("SDLPal static thread storage is outside Secondary SRAM")
        if thread_end - thread_start < PAL_THREAD_STACK_BYTES:
            errors.append("SDLPal static thread section is smaller than 24 KiB")
        if audio_start < origin or audio_end > origin + length:
            errors.append("SDLPal audio storage is outside Secondary SRAM")
        if audio_start < thread_end or audio_end < audio_start:
            errors.append("SDLPal audio storage overlaps static thread storage")
        if audio_end - audio_start > PAL_AUDIO_MAX_BYTES:
            errors.append("SDLPal audio storage exceeds 48 KiB")
        if symbols["__HeapBase"] < audio_end:
            errors.append("primary heap overlaps SDLPal audio storage")
        if symbols["__HeapBase"] < thread_end:
            errors.append("primary heap overlaps SDLPal static thread storage")

    has_scanout = "graphics_scanout_storage" in symbols
    if rotation in (90, 270) and not has_scanout:
        errors.append("landscape rotation is missing the VG-Lite scanout buffer")
    if rotation in (0, 180) and has_scanout:
        errors.append("portrait rotation unexpectedly links a scanout buffer")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", required=True, type=pathlib.Path)
    parser.add_argument("--map", required=True, dest="map_file", type=pathlib.Path)
    parser.add_argument("--nm", default="arm-none-eabi-nm")
    parser.add_argument("--rotation", required=True, type=int)
    args = parser.parse_args()

    if not args.elf.is_file():
        parser.error(f"ELF not found: {args.elf}")
    if not args.map_file.is_file():
        parser.error(f"map not found: {args.map_file}")

    try:
        nm_result = subprocess.run(
            [args.nm, "-n", str(args.elf)],
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"ERROR: unable to run nm: {error}", file=sys.stderr)
        return 2
    symbols = parse_nm_output(nm_result.stdout)
    regions, sections = parse_map(args.map_file.read_text(encoding="utf-8"))
    errors = validate_layout(symbols, regions, args.rotation)

    framebuffer = sections.get("pal_framebuffer")
    if framebuffer is None:
        errors.append("map is missing .pal_framebuffer")
    elif framebuffer[1] != PAL_FRAMEBUFFER_BYTES:
        errors.append(
            f"map .pal_framebuffer must be 131072 bytes, got {framebuffer[1]}"
        )
    if "cy_gpu_buf" not in sections:
        errors.append("map is missing .cy_gpu_buf")
    thread_section = sections.get("sdlpal_thread")
    if thread_section is None:
        errors.append("map is missing .sdlpal_thread")
    elif thread_section[1] < PAL_THREAD_STACK_BYTES:
        errors.append("map .sdlpal_thread is smaller than 24 KiB")
    audio_section = sections.get("sdlpal_audio")
    if audio_section is None:
        errors.append("map is missing .sdlpal_audio")
    elif audio_section[1] > PAL_AUDIO_MAX_BYTES:
        errors.append("map .sdlpal_audio exceeds 48 KiB")

    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1

    gfx_bytes = symbols["__cy_gpu_buf_end__"] - symbols["__cy_gpu_buf_start__"]
    indexed_bytes = (
        symbols["__lcd_indexed_staging_end__"]
        - symbols["__lcd_indexed_staging_start__"]
    )
    large_bytes = (
        symbols["__sdlpal_large_end__"] - symbols["__sdlpal_large_start__"]
    )
    save_bytes = (
        symbols["__sdlpal_save_end__"] - symbols["__sdlpal_save_start__"]
    )
    resource_bytes = (
        symbols["__sdlpal_resource_end__"]
        - symbols["__sdlpal_resource_start__"]
    )
    thread_bytes = (
        symbols["__sdlpal_thread_end__"] - symbols["__sdlpal_thread_start__"]
    )
    audio_bytes = (
        symbols["__sdlpal_audio_end__"] - symbols["__sdlpal_audio_start__"]
    )
    dtcm_headroom = symbols["__StackLimit"] - symbols["__bss_end__"]
    itcm_bytes = (
        symbols["__sdlpal_itcm_end__"] - symbols["__sdlpal_itcm_start__"]
    )
    itcm_origin, itcm_length = regions["m55_code_INTERNAL"]
    itcm_reserved = (
        itcm_origin + itcm_length - symbols["__ram_vectors_end__"]
    )
    print(
        f"PASS rotation={args.rotation} framebuffer={PAL_FRAMEBUFFER_BYTES} "
        f"gfx={gfx_bytes} indexed={indexed_bytes} large={large_bytes} "
        f"save={save_bytes} resource={resource_bytes} "
        f"thread={thread_bytes} audio={audio_bytes} "
        f"itcm={itcm_bytes} itcm_reserved={itcm_reserved} "
        f"dtcm_headroom={dtcm_headroom}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
