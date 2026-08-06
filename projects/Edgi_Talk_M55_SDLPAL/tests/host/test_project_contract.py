from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
BSP_ROOT = ROOT.parents[1]


class ProjectContractTest(unittest.TestCase):
    def test_gcc_build_uses_o3(self):
        build_config = (ROOT / "rtconfig.py").read_text(encoding="utf-8")
        gcc_config = build_config.split("if PLATFORM == 'gcc':", 1)[1].split(
            "elif PLATFORM == 'armclang':", 1
        )[0]
        active_lines = {
            line.strip()
            for line in gcc_config.splitlines()
            if not line.lstrip().startswith("#")
        }

        self.assertIn("CFLAGS += ' -g -O3'", active_lines)
        self.assertIn("CFLAGS += ' -O3'", active_lines)
        self.assertNotIn("CFLAGS += ' -g -O0'", active_lines)
        self.assertNotIn("CFLAGS += ' -O2'", active_lines)

    def test_rotation_matrix_accepts_any_valid_baseline(self):
        build_matrix = (
            ROOT / "tools" / "build_matrix.ps1"
        ).read_text(encoding="utf-8")

        self.assertIn(
            "if (-not [regex]::IsMatch($originalHeader, $headerPattern))",
            build_matrix,
        )
        self.assertIn(
            "CONFIG_BSP_LCD_ROTATION_(?:0|90|180|270)=y",
            build_matrix,
        )
        self.assertIn(
            "CONFIG_BSP_LCD_ROTATION_DEGREES=(?:0|90|180|270)",
            build_matrix,
        )
        self.assertNotIn("Default portrait rotation block", build_matrix)
        self.assertNotIn("if ($updated -eq $originalHeader)", build_matrix)

    def test_target_contract(self):
        sconstruct = (ROOT / "SConstruct").read_text(encoding="utf-8")
        project_kconfig = (ROOT / "Kconfig").read_text(encoding="utf-8")
        shared_kconfig = (
            BSP_ROOT / "libraries" / "M55_Config" / "Kconfig"
        ).read_text(encoding="utf-8")
        config = (ROOT / ".config").read_text(encoding="utf-8")
        rtconfig = (ROOT / "rtconfig.h").read_text(encoding="utf-8")
        application = (ROOT / "applications" / "main.c").read_text(
            encoding="utf-8"
        )
        linker = (
            ROOT / "board" / "linker_scripts" / "link.ld"
        ).read_text(encoding="utf-8")
        lcd_header_path = ROOT / "platform" / "pal_lcd_api.h"
        self.assertTrue(lcd_header_path.is_file())
        lcd_header = lcd_header_path.read_text(encoding="utf-8")
        lcd_source = (
            BSP_ROOT / "libraries" / "HAL_Drivers" / "drv_lcd.c"
        ).read_text(encoding="utf-8")
        vg_lite_hal = (
            BSP_ROOT
            / "libraries"
            / "components"
            / "mtb-device-support-pse8xxgp"
            / "pdl"
            / "drivers"
            / "third_party"
            / "COMPONENT_GFXSS"
            / "vsi"
            / "gcnano"
            / "vg_lite_hal.c"
        ).read_text(encoding="utf-8")
        display_port = (
            ROOT / "platform" / "pal_display_port.c"
        ).read_text(encoding="utf-8")

        self.assertIn(
            "#define PAL_ENGINE_STACK_BYTES (24u * 1024u)", application
        )
        self.assertIn("static struct rt_thread engine_thread_storage", application)
        self.assertIn(
            "static rt_uint8_t engine_stack[PAL_ENGINE_STACK_BYTES]", application
        )
        self.assertIn('section(".sdlpal_thread")', application)
        self.assertIn("rt_thread_init(\n        &engine_thread_storage", application)
        self.assertIn("rt_thread_detach(&engine_thread_storage)", application)
        self.assertNotIn("rt_thread_create(", application)
        self.assertNotIn("rt_thread_delete(", application)
        self.assertIn(".sdlpal_thread (NOLOAD)", linker)
        self.assertIn("__sdlpal_thread_start__", linker)
        self.assertIn("__sdlpal_thread_end__", linker)

        self.assertNotIn("lvgl_9.2.0/SConscript", sconstruct)
        self.assertIn("env.Append(CPPFLAGS=['-include', 'rtconfig.h'])", sconstruct)
        self.assertIn("config BSP_USING_SDLPAL", project_kconfig)
        self.assertIn("config BSP_LCD_VGLITE_INDEXED", project_kconfig)
        self.assertNotIn("config BSP_LCD_VGLITE_INDEXED", shared_kconfig)
        self.assertIn("CONFIG_BSP_USING_SDLPAL=y", config)
        self.assertIn("#define BSP_USING_SDLPAL", rtconfig)
        self.assertFalse(
            (BSP_ROOT / "libraries" / "HAL_Drivers" / "drv_lcd.h").exists()
        )
        self.assertNotIn("CONFIG_USING_LVGL=y", config)
        self.assertNotIn("CONFIG_BSP_USING_LVGL=y", config)
        self.assertIn("CONFIG_RT_USING_RTC=y", config)
        self.assertIn("CONFIG_RT_USING_SOFT_RTC=y", config)
        self.assertIn("# CONFIG_BSP_USING_RTC is not set", config)
        self.assertIn("#define RT_USING_RTC", rtconfig)
        self.assertIn("#define RT_USING_SOFT_RTC", rtconfig)
        self.assertNotIn("#define BSP_USING_RTC", rtconfig)

        for setting in (
            "CONFIG_BSP_USING_LCD=y",
            "CONFIG_BSP_USING_HYPERAM=y",
            "CONFIG_BSP_USING_FILESYSTEM=y",
            "CONFIG_BSP_USING_SDCARD=y",
            "CONFIG_BSP_USING_SDIO1=y",
            "CONFIG_RT_USING_DFS_ELMFAT=y",
            "CONFIG_BSP_LCD_VGLITE_INDEXED=y",
        ):
            self.assertIn(setting, config)

        rotation_values = (0, 90, 180, 270)
        active_rotations = [
            value
            for value in rotation_values
            if f"CONFIG_M55_BSP_LCD_ROTATION_{value}=y" in config
        ]
        self.assertEqual(len(active_rotations), 1)
        rotation = active_rotations[0]
        self.assertIn(f"CONFIG_BSP_LCD_ROTATION_{rotation}=y", config)
        self.assertIn(
            f"CONFIG_BSP_LCD_ROTATION_DEGREES={rotation}", config
        )
        self.assertIn(
            f"#define M55_BSP_LCD_ROTATION_{rotation}", rtconfig
        )
        self.assertIn(f"#define BSP_LCD_ROTATION_{rotation}", rtconfig)
        self.assertIn(
            f"#define BSP_LCD_ROTATION_DEGREES {rotation}", rtconfig
        )
        if rotation in (90, 270):
            self.assertIn(
                "CONFIG_BSP_LCD_ROTATION_BACKEND_VGLITE=y", config
            )
            self.assertIn(
                "#define BSP_LCD_ROTATION_BACKEND_VGLITE", rtconfig
            )
        if rotation == 180:
            self.assertIn("CONFIG_BSP_LCD_PANEL_SCAN_ROTATE_180=y", config)
            self.assertIn(
                "#define BSP_LCD_PANEL_SCAN_ROTATE_180", rtconfig
            )

        self.assertIn("#define BSP_LCD_VGLITE_INDEXED", rtconfig)
        self.assertIn("lcd_blit_indexed8", lcd_header)
        self.assertIn("VG_LITE_INDEX_8", lcd_source)
        self.assertIn("vg_lite_set_CLUT", lcd_source)
        self.assertIn(".cy_gpu_buf.sdlpal_indexed", lcd_source)
        self.assertIn("static rt_bool_t vglite_failed = RT_FALSE;", lcd_source)
        self.assertIn("if (vglite_failed)", lcd_source)
        self.assertIn("vg_lite_hal_free(device);", vg_lite_hal)
        self.assertNotIn("BSP_USING_SDLPAL", vg_lite_hal)
        self.assertIn("__lcd_indexed_staging_start__", linker)
        self.assertIn("__lcd_indexed_staging_end__", linker)
        self.assertNotIn("BSP_USING_LVGL", display_port)

    def test_shared_changes_are_sdlpal_guarded(self):
        lcd = (
            BSP_ROOT / "libraries" / "HAL_Drivers" / "drv_lcd.c"
        ).read_text(encoding="utf-8")
        touch = (
            BSP_ROOT
            / "libraries"
            / "Common"
            / "board"
            / "ports"
            / "display_panels"
            / "drv_touch.c"
        ).read_text(encoding="utf-8")
        vg_hal = (
            BSP_ROOT
            / "libraries"
            / "components"
            / "mtb-device-support-pse8xxgp"
            / "pdl"
            / "drivers"
            / "third_party"
            / "COMPONENT_GFXSS"
            / "vsi"
            / "gcnano"
            / "vg_lite_hal.c"
        ).read_text(encoding="utf-8")

        self.assertIn("defined(BSP_USING_SDLPAL)", lcd)
        self.assertIn("defined(BSP_LCD_VGLITE_INDEXED)", lcd)
        self.assertIn("#ifdef BSP_USING_SDLPAL", lcd)
        self.assertIn("buffer->format = VG_LITE_RGB565;", lcd)

        self.assertIn("defined(BSP_USING_SDLPAL)", touch)
        self.assertIn("#else", touch)
        self.assertIn("rt_size_t max_points = ST7102_MAX_TOUCH;", touch)
        self.assertIn("defined(ST7102_HOST_TEST)", touch)

        self.assertIn("vg_lite_hal_free(device);", vg_hal)
        self.assertNotIn("BSP_USING_SDLPAL", vg_hal)

    def test_hyperram_is_not_a_transparent_heap_fallback(self):
        config = (ROOT / ".config").read_text(encoding="utf-8")

        self.assertIn("CONFIG_RT_USING_MEMHEAP_AS_HEAP=y", config)
        self.assertNotIn("CONFIG_RT_USING_MEMHEAP_AUTO_BINDING=y", config)
        self.assertNotIn("CONFIG_PKG_USING_CPU_USAGE=y", config)

    def test_engine_import_contract(self):
        engine_config = (ROOT / "platform" / "pal_config.h").read_text(
            encoding="utf-8"
        )
        build_script = (ROOT / "sdlpal" / "SConscript").read_text(
            encoding="utf-8"
        )
        provenance = (ROOT / "sdlpal" / "UPSTREAM.md").read_text(
            encoding="utf-8"
        )

        for feature in (
            "PAL_HAS_MP3",
            "PAL_HAS_OGG",
            "PAL_HAS_OPUS",
            "PAL_HAS_NATIVEMIDI",
            "PAL_HAS_JOYSTICKS",
            "PAL_HAS_TOUCH",
            "PAL_HAS_MOUSE",
        ):
            self.assertRegex(engine_config, rf"#define\s+{feature}\s+0")

        self.assertNotIn("PAL_CONTRACT_NO_AUDIO", build_script)
        self.assertNotIn("unix/contract_noaudio.c", build_script)
        self.assertIn("PAL_PSOC_DIRECT_INDEXED", build_script)
        self.assertIn("PAL_SDL_SHIM_DYNAMIC_SURFACES", build_script)
        self.assertIn("23177627e619731188591288215dc2a61d884ae7", provenance)

        forbidden_modes = (
            "MEM_LEVEL1",
            "MEM_LEVEL2",
            "PAL_NO_RUNTIME_HEAP",
            "PAL_NO_RUNTIME_DECOMPRESS",
            "PAL_EXTREME_TWO_SCREENS",
        )
        combined = engine_config + build_script
        for mode in forbidden_modes:
            self.assertNotIn(mode, combined)

    def test_audio_integration_contract(self):
        project_kconfig = (ROOT / "Kconfig").read_text(encoding="utf-8")
        config = (ROOT / ".config").read_text(encoding="utf-8")
        rtconfig = (ROOT / "rtconfig.h").read_text(encoding="utf-8")
        engine_build = (ROOT / "sdlpal" / "SConscript").read_text(
            encoding="utf-8"
        )
        audio_build_path = ROOT / "audio" / "SConscript"
        i2s_header = (
            BSP_ROOT / "libraries" / "HAL_Drivers" / "drv_i2s.h"
        ).read_text(encoding="utf-8")
        i2s_source = (
            BSP_ROOT / "libraries" / "HAL_Drivers" / "drv_i2s.c"
        ).read_text(encoding="utf-8")

        self.assertIn("select BSP_USING_AUDIO", project_kconfig)
        self.assertIn("select BSP_USING_AUDIO_PLAY", project_kconfig)
        for setting in (
            "CONFIG_RT_USING_AUDIO=y",
            "CONFIG_RT_AUDIO_REPLAY_MP_BLOCK_SIZE=512",
            "CONFIG_RT_AUDIO_REPLAY_MP_BLOCK_COUNT=2",
            "CONFIG_BSP_USING_AUDIO=y",
            "CONFIG_BSP_USING_AUDIO_PLAY=y",
        ):
            self.assertIn(setting, config)
        for define in (
            "#define RT_USING_AUDIO",
            "#define RT_AUDIO_REPLAY_MP_BLOCK_SIZE 512",
            "#define RT_AUDIO_REPLAY_MP_BLOCK_COUNT 2",
            "#define BSP_USING_AUDIO",
            "#define BSP_USING_AUDIO_PLAY",
        ):
            self.assertIn(define, rtconfig)

        self.assertNotIn("unix/contract_noaudio.c", engine_build)
        self.assertNotIn("PAL_CONTRACT_NO_AUDIO", engine_build)
        self.assertTrue(audio_build_path.is_file())
        audio_build = audio_build_path.read_text(encoding="utf-8")
        self.assertIn("PAL_NO_RUNTIME_HEAP", audio_build)
        self.assertIn("USE_RIX_EXTRA_INIT", audio_build)

        self.assertIn("#if defined(BSP_USING_SDLPAL)", i2s_header)
        self.assertIn("PLAYBACK_DATA_FRAME_SIZE", i2s_header)
        self.assertIn("(512)", i2s_header)
        self.assertIn("#if defined(BSP_USING_SDLPAL)", i2s_source)
        self.assertIn("TX_FIFO_SIZE", i2s_source)
        self.assertIn("(1024)", i2s_source)

    def test_audio_port_uses_bounded_static_secondary_sram(self):
        port_path = ROOT / "audio" / "pal_audio_port.c"
        linker = (ROOT / "board" / "linker_scripts" / "link.ld").read_text(
            encoding="utf-8"
        )

        self.assertTrue(port_path.is_file())
        port = port_path.read_text(encoding="utf-8")
        self.assertIn("PAL_AUDIO_BLOCK_SAMPLES 256u", port)
        self.assertIn("PAL_AUDIO_STACK_BYTES (8u * 1024u)", port)
        self.assertIn('section(".sdlpal_audio")', port)
        self.assertIn("static struct rt_thread", port)
        self.assertIn("rt_thread_init(", port)
        self.assertNotIn("rt_thread_create(", port)
        self.assertIn('rt_device_find("sound0")', port)
        self.assertIn("AUDIO_CTL_CONFIGURE", port)
        self.assertIn("AUDIO_TYPE_OUTPUT", port)
        self.assertIn("AUDIO_DSP_PARAM", port)

        self.assertIn(".sdlpal_audio (NOLOAD)", linker)
        self.assertIn("__sdlpal_audio_start__", linker)
        self.assertIn("__sdlpal_audio_end__", linker)
        self.assertIn("SIZEOF(.sdlpal_audio) <= 0xc000", linker)
        zero_table = re.search(
            r"\.zero\.table\s*:[\s\S]*?__zero_table_end__\s*=\s*\.;",
            linker,
        )
        self.assertIsNotNone(zero_table)
        self.assertIn("LONG(__sdlpal_audio_start__)", zero_table.group(0))
        self.assertIn(
            "LONG((__sdlpal_audio_end__ - __sdlpal_audio_start__)/4)",
            zero_table.group(0),
        )
        self.assertRegex(
            linker,
            r"\.sdlpal_audio\s*\(NOLOAD\)[\s\S]*?}\s*>\s*m55_data_secondary",
        )

    def test_audio_contract_is_project_owned(self):
        contract_path = ROOT / "audio" / "pal_audio_contract.c"
        resources_path = ROOT / "audio" / "pal_audio_resources.c"
        backend = (ROOT / "platform" / "pal_backend_stubs.c").read_text(
            encoding="utf-8"
        )
        engine_build = (ROOT / "sdlpal" / "SConscript").read_text(
            encoding="utf-8"
        )

        self.assertTrue(contract_path.is_file())
        self.assertTrue(resources_path.is_file())
        contract = contract_path.read_text(encoding="utf-8")
        resources = resources_path.read_text(encoding="utf-8")
        self.assertEqual(contract.count("AUDIODEVICE gAudioDevice"), 1)
        for function in (
            "AUDIO_OpenDevice",
            "AUDIO_CD_Available",
            "AUDIO_CloseDevice",
            "AUDIO_GetDeviceSpec",
            "AUDIO_IncreaseVolume",
            "AUDIO_DecreaseVolume",
            "AUDIO_PlayMusic",
            "AUDIO_PlayCDTrack",
            "AUDIO_PlaySound",
            "AUDIO_EnableMusic",
            "AUDIO_MusicEnabled",
            "AUDIO_EnableSound",
            "AUDIO_SoundEnabled",
            "AUDIO_Lock",
            "AUDIO_Unlock",
        ):
            self.assertIn(function + "(", contract)
        self.assertIn('"mus.mkf"', resources)
        self.assertIn('"voc.mkf"', resources)
        self.assertIn("PAL_MKFGetChunkSize", resources)
        self.assertIn("PAL_MKFReadChunk", resources)
        self.assertIn("pal_cold_alloc", resources)
        self.assertIn("PAL_MEMORY_TAG_RESOURCE", resources)
        self.assertNotIn("RIX_Init(", backend)
        self.assertNotIn("SOUND_Init(", backend)
        self.assertNotIn("unix/contract_noaudio.c", engine_build)

    def test_audio_diagnostics_and_documentation_contract(self):
        diagnostics_path = ROOT / "audio" / "pal_audio_diagnostics.c"
        diagnostics_header_path = ROOT / "audio" / "pal_audio_diagnostics.h"
        memory = (ROOT / "platform" / "pal_memory.c").read_text(
            encoding="utf-8"
        )
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        upstream = (ROOT / "sdlpal" / "UPSTREAM.md").read_text(
            encoding="utf-8"
        )
        elf_check = (ROOT / "tools" / "check_elf.py").read_text(
            encoding="utf-8"
        )
        i2s_source = (
            BSP_ROOT / "libraries" / "HAL_Drivers" / "drv_i2s.c"
        ).read_text(encoding="utf-8")

        self.assertTrue(diagnostics_path.is_file())
        self.assertTrue(diagnostics_header_path.is_file())
        diagnostics = diagnostics_path.read_text(encoding="utf-8")
        header = diagnostics_header_path.read_text(encoding="utf-8")
        for metric in (
            "rendered_blocks",
            "active_voices",
            "peak_voices",
            "render_max_us",
            "write_max_us",
            "hardware_underruns",
            "sound_drops",
            "cache_current_bytes",
            "cache_peak_bytes",
            "audio_stack_used_bytes",
        ):
            self.assertIn(metric, header)
        self.assertIn("MSH_CMD_EXPORT(pal_audio", diagnostics)
        self.assertIn("pal_audio_diagnostics_get", memory)
        self.assertIn("PAL_AUDIO_MAX_BYTES = 48 * 1024", elf_check)
        self.assertIn("#if defined(BSP_USING_SDLPAL)", i2s_source)
        self.assertIn("sdlpal_i2s_underruns", i2s_source)
        self.assertIn("sdlpal_reset_playback_state", i2s_source)
        self.assertIn("rt_mq_control(snd_dev->tx_mq", i2s_source)
        self.assertIn("rt_sem_control(snd_dev->tx_sem", i2s_source)
        self.assertIn("rt_data_queue_reset(&audio->replay->queue)", i2s_source)
        self.assertIn("if (tx_buff == RT_NULL)", i2s_source)
        self.assertLess(
            i2s_source.index("if (tx_buff == RT_NULL)"),
            i2s_source.index("rt_memset(tx_buff, 0, TX_FIFO_SIZE)"),
        )
        for text in ("mus.mkf", "voc.mkf", "sound0", "pal_audio", "10"):
            self.assertIn(text, readme)
        self.assertIn("audio/third_party", upstream)

        contract = (ROOT / "audio" / "pal_audio_contract.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("PAL_AUDIO_MAX_LIVE_HANDLES", contract)
        self.assertIn(
            "pal_audio_release_capacity_covers_all_owners", contract
        )
        self.assertIn(
            "RT_ASSERT(audio_state.release_count < "
            "PAL_AUDIO_RELEASE_CAPACITY)",
            contract,
        )

    def test_indexed_framebuffers_are_fixed_in_dtcm(self):
        memory_header = (ROOT / "platform" / "pal_memory.h").read_text(
            encoding="utf-8"
        )
        memory_source = (ROOT / "platform" / "pal_memory.c").read_text(
            encoding="utf-8"
        )
        linker = (ROOT / "board" / "linker_scripts" / "link.ld").read_text(
            encoding="utf-8"
        )
        video = (ROOT / "sdlpal" / "upstream" / "video.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("PAL_INDEXED_SLOT_BYTES (64u * 1024u)", memory_header)
        self.assertEqual(memory_source.count("[PAL_INDEXED_SLOT_BYTES]"), 2)
        self.assertIn('section(".pal_framebuffer")', memory_source)
        self.assertIn(".pal_framebuffer (NOLOAD)", linker)
        self.assertIn("SIZEOF(.pal_framebuffer) == 0x20000", linker)
        self.assertIn("PAL_PSOC_DIRECT_INDEXED", video)
        self.assertIn("pal_framebuffer_primary", video)
        self.assertIn("PalEngineBridge_RenderPresentIndexed", video)

    def test_upstream_sources_do_not_depend_on_platform_scratch(self):
        for name in ("battle.c", "ending.c", "text.c", "uigame.c"):
            source = (ROOT / "sdlpal" / "upstream" / name).read_text(
                encoding="utf-8"
            )
            self.assertNotIn("pal_scratch", source, name)

        self.assertFalse((ROOT / "platform" / "pal_scratch.c").exists())
        self.assertFalse((ROOT / "platform" / "pal_scratch.h").exists())

    def test_pal_large_storage_is_platform_owned_gfx_memory(self):
        storage_header = ROOT / "platform" / "pal_large_storage.h"
        self.assertTrue(storage_header.is_file())

        header = storage_header.read_text(encoding="utf-8")
        build_script = (ROOT / "sdlpal" / "SConscript").read_text(
            encoding="utf-8"
        )
        linker = (ROOT / "board" / "linker_scripts" / "link.ld").read_text(
            encoding="utf-8"
        )

        self.assertIn("#define PAL_LARGE static", header)
        self.assertIn('section(".cy_gpu_buf.sdlpal_large")', header)
        self.assertIn("-include pal_large_storage.h", build_script)
        self.assertIn("__sdlpal_large_start__", linker)
        self.assertIn("KEEP(*(.cy_gpu_buf.sdlpal_large))", linker)
        self.assertIn("__sdlpal_large_end__", linker)

    def test_surface_storage_has_a_fixed_gfx_fallback_pool(self):
        storage_header = ROOT / "platform" / "pal_surface_storage.h"
        storage_source = ROOT / "platform" / "pal_surface_storage.c"
        linker = (ROOT / "board" / "linker_scripts" / "link.ld").read_text(
            encoding="utf-8"
        )
        platform_build = (ROOT / "platform" / "SConscript").read_text(
            encoding="utf-8"
        )

        self.assertTrue(storage_header.is_file())
        self.assertTrue(storage_source.is_file())
        self.assertIn("PAL_SURFACE_STORAGE_GFX", storage_source.read_text(
            encoding="utf-8"
        ))
        self.assertIn('section(".cy_gpu_buf.sdlpal_surface")', storage_source.read_text(
            encoding="utf-8"
        ))
        self.assertIn("__sdlpal_surface_start__", linker)
        self.assertIn("__sdlpal_surface_end__", linker)
        self.assertIn("0x20000", linker)
        self.assertIn("Glob('*.c')", platform_build)

    def test_engine_heap_has_a_fixed_gfx_fallback_pool(self):
        heap_source = (
            ROOT / "platform" / "pal_engine_heap.c"
        ).read_text(encoding="utf-8")
        linker = (ROOT / "board" / "linker_scripts" / "link.ld").read_text(
            encoding="utf-8"
        )

        self.assertIn('section(".cy_gpu_buf.sdlpal_resource")', heap_source)
        self.assertIn("__sdlpal_resource_start__", linker)
        self.assertIn("KEEP(*(.cy_gpu_buf.sdlpal_resource))", linker)
        self.assertIn("__sdlpal_resource_end__", linker)
        self.assertIn(
            "(__sdlpal_resource_end__ - __sdlpal_resource_start__) == 0x20000",
            linker,
        )

    def test_save_path_is_platform_owned_and_bounded(self):
        hook_header = ROOT / "platform" / "pal_engine_io_hooks.h"
        adapter_source = ROOT / "platform" / "pal_engine_io.c"
        self.assertTrue(hook_header.is_file())
        self.assertTrue(adapter_source.is_file())

        hooks = hook_header.read_text(encoding="utf-8")
        adapter = adapter_source.read_text(encoding="utf-8")
        build_script = (ROOT / "sdlpal" / "SConscript").read_text(
            encoding="utf-8"
        )
        linker = (ROOT / "board" / "linker_scripts" / "link.ld").read_text(
            encoding="utf-8"
        )

        for libc_name, platform_name in (
            ("malloc", "pal_engine_malloc"),
            ("calloc", "pal_engine_calloc"),
            ("realloc", "pal_engine_realloc"),
            ("free", "pal_engine_free"),
            ("fopen", "pal_engine_fopen"),
            ("fwrite", "pal_engine_fwrite"),
            ("fclose", "pal_engine_fclose"),
        ):
            self.assertIn(f"#define {libc_name} {platform_name}", hooks)

        self.assertIn("-include pal_engine_io_hooks.h", build_script)
        self.assertIn('section(".cy_gpu_buf.sdlpal_save")', adapter)
        self.assertIn("PAL_SAVE_RESERVE_BYTES (192u * 1024u)", adapter)
        self.assertIn("pal_save_write_chunked", adapter)
        self.assertIn("PAL_SAVE_IO_CHUNK_BYTES", adapter)
        self.assertIn("_IONBF", adapter)
        self.assertIn("pal_engine_heap_malloc", adapter)
        self.assertIn("pal_engine_heap_fallback_malloc", adapter)
        self.assertIn("pal_engine_heap_calloc", adapter)
        self.assertIn("pal_engine_heap_realloc", adapter)
        self.assertIn("pal_engine_heap_free", adapter)

        self.assertIn("__sdlpal_save_start__", linker)
        self.assertIn("KEEP(*(.cy_gpu_buf.sdlpal_save))", linker)
        self.assertIn("__sdlpal_save_end__", linker)
        self.assertIn("== 0x30000", linker)

        for name in ("global.c", "uigame.c"):
            upstream = (ROOT / "sdlpal" / "upstream" / name).read_text(
                encoding="utf-8"
            )
            for hook in (
                "pal_engine_malloc",
                "pal_engine_calloc",
                "pal_engine_realloc",
                "pal_engine_free",
                "pal_engine_fopen",
                "pal_engine_fwrite",
                "pal_engine_fclose",
            ):
                self.assertNotIn(hook, upstream, name)

    def test_vglite_uses_lcd_compatible_rgb565_channel_order(self):
        lcd_driver = (BSP_ROOT / "libraries" / "HAL_Drivers" / "drv_lcd.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("#ifdef BSP_USING_SDLPAL", lcd_driver)
        self.assertIn("buffer->format = VG_LITE_BGR565;", lcd_driver)
        self.assertIn("buffer->format = VG_LITE_RGB565;", lcd_driver)

    def test_builtin_font_tables_are_flash_resident(self):
        font_glyph = (ROOT / "sdlpal" / "upstream" / "fontglyph.h").read_text(
            encoding="utf-8"
        )
        ascii_font = (ROOT / "sdlpal" / "upstream" / "ascii.h").read_text(
            encoding="utf-8"
        )
        font_source = (ROOT / "sdlpal" / "upstream" / "font.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("static const unsigned char unicode_font", font_glyph)
        self.assertIn("const unsigned char font_width", font_glyph)
        self.assertIn("static const unsigned char iso_font", ascii_font)
        self.assertIn("PAL_PSOC_DIRECT_INDEXED", font_source)

    def test_touch_port_uses_polling_mode(self):
        touch_port = (ROOT / "platform" / "pal_touch_port.c").read_text(
            encoding="utf-8"
        )
        platform_build = (ROOT / "platform" / "SConscript").read_text(
            encoding="utf-8"
        )

        self.assertIn("rt_device_open(touch_device, RT_DEVICE_FLAG_RDONLY)", touch_port)
        self.assertNotIn("RT_DEVICE_FLAG_INT_RX", touch_port)
        self.assertIn("pal_touch_poll_cache_sample_due", touch_port)
        self.assertIn("pal_touch_poll_cache_copy", touch_port)
        self.assertIn("pal_touch_poll_cache_store", touch_port)
        self.assertIn("Glob('*.c')", platform_build)

    def test_touch_driver_respects_the_caller_buffer_capacity(self):
        touch_driver = (
            BSP_ROOT
            / "libraries"
            / "Common"
            / "board"
            / "ports"
            / "display_panels"
            / "drv_touch.c"
        ).read_text(encoding="utf-8")

        self.assertIn("rt_size_t writable_points = read_num;", touch_driver)
        self.assertIn(
            "if ((buf == RT_NULL) || (writable_points == 0))", touch_driver
        )
        self.assertIn("#define ST7102_PARSE_LIMIT parse_points", touch_driver)
        self.assertIn("for (count = 0; count < ST7102_PARSE_LIMIT; count++)", touch_driver)
        self.assertIn("for (; count < writable_points; count++)", touch_driver)
        self.assertIn("for (; count < ST7102_MAX_TOUCH; count++)", touch_driver)

    def test_immediate_touch_feedback_is_diagnosable(self):
        memory_source = (ROOT / "platform" / "pal_memory.c").read_text(
            encoding="utf-8"
        )
        project_readme = (ROOT / "README.md").read_text(encoding="utf-8")

        for metric in (
            "control_update_count",
            "control_last_microseconds",
            "control_max_microseconds",
        ):
            self.assertIn(metric, memory_source)

        self.assertIn("约 10 ms", project_readme)
        self.assertIn("缓存触点", project_readme)
        self.assertIn("变化按钮矩形", project_readme)

    def test_delivery_tools_and_documentation(self):
        project_readme = (ROOT / "README.md").read_text(encoding="utf-8")
        bsp_manifest = (
            BSP_ROOT / "sdk-bsp-psoc_e84-edgi-talk.yaml"
        ).read_text(encoding="utf-8")

        for name in (
            "abc.mkf",
            "ball.mkf",
            "data.mkf",
            "f.mkf",
            "fbp.mkf",
            "fire.mkf",
            "gop.mkf",
            "map.mkf",
            "mgo.mkf",
            "pat.mkf",
            "rgm.mkf",
            "rng.mkf",
            "sss.mkf",
            "word.dat",
            "m.msg",
        ):
            self.assertIn(name, project_readme)

        for item in (
            "/sdcard/pal/save",
            "pal_mem",
            "build_matrix.ps1",
            "E01",
            "E02",
            "E03",
            "E04",
            "E05",
            "audio",
        ):
            self.assertIn(item, project_readme)

        self.assertTrue((ROOT / "tools" / "check_elf.py").is_file())
        self.assertTrue((ROOT / "tools" / "check_stack_usage.py").is_file())
        self.assertTrue((ROOT / "tools" / "build_matrix.ps1").is_file())
        self.assertIn("check_stack_usage.py", project_readme)
        self.assertIn("project_name: Edgi_Talk_M55_SDLPAL", bsp_manifest)


if __name__ == "__main__":
    unittest.main()
