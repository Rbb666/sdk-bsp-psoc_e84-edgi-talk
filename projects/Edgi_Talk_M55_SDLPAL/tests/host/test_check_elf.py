import importlib.util
import pathlib
import unittest


MODULE_PATH = pathlib.Path(__file__).parents[2] / "tools" / "check_elf.py"
SPEC = importlib.util.spec_from_file_location("check_elf", MODULE_PATH)
check_elf = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(check_elf)


class ElfValidationTests(unittest.TestCase):
    def setUp(self):
        self.symbols = {
            "main": 0x60800000,
            "__pal_framebuffer_start__": 0x20000000,
            "__pal_framebuffer_end__": 0x20020000,
            "__bss_end__": 0x20038000,
            "__StackLimit": 0x2003F000,
            "__sdlpal_thread_start__": 0x26060028,
            "__sdlpal_thread_end__": 0x260660B8,
            "__HeapBase": 0x260660B8,
            "__cy_gpu_buf_start__": 0x26200000,
            "__lcd_indexed_staging_start__": 0x262F9C00,
            "__lcd_indexed_staging_end__": 0x26309600,
            "__sdlpal_large_start__": 0x26309600,
            "__sdlpal_large_end__": 0x26309600 + 593408,
            "__sdlpal_save_start__": 0x26309600 + 593408,
            "__sdlpal_save_end__": 0x26309600 + 593408 + 196608,
            "__cy_gpu_buf_end__": 0x26309600 + 593408 + 196608,
            "__sdlpal_itcm_start__": 0x00001000,
            "__sdlpal_itcm_end__": 0x0001D000,
            "__ram_vectors_end__": 0x0001D800,
            "PAL_GameMain": 0x00001000,
            "PAL_StartFrame": 0x00001100,
            "PAL_MakeScene": 0x00001200,
            "PAL_MapBlitToSurface": 0x00001300,
            "PAL_RLEBlitToSurface": 0x00001400,
            "PAL_InterpretInstruction": 0x00001500,
            "PAL_BattleStartFrame": 0x00001600,
            "SDL_UpperBlit": 0x00001700,
        }
        self.regions = {
            "m55_code_INTERNAL": (0x00000000, 0x40000),
            "m55_data_INTERNAL": (0x20000000, 0x40000),
            "m55_data_secondary": (0x26060000, 0x160000),
            "gfx_mem": (0x26200000, 0x300000),
        }

    def test_valid_portrait_layout(self):
        self.assertEqual(
            check_elf.validate_layout(self.symbols, self.regions, 0), []
        )

    def test_rejects_forbidden_and_overflowing_layouts(self):
        self.symbols["lv_timer_handler"] = 0x60801000
        self.symbols["audio_thread_entry"] = 0x60802000
        self.symbols["__pal_framebuffer_end__"] = 0x20021000
        self.symbols["__cy_gpu_buf_end__"] = 0x26500001

        errors = check_elf.validate_layout(self.symbols, self.regions, 0)
        joined = "\n".join(errors)
        self.assertIn("LVGL", joined)
        self.assertIn("audio", joined)
        self.assertIn("131072", joined)
        self.assertIn("GFX", joined)

    def test_landscape_requires_scanout_buffer(self):
        errors = check_elf.validate_layout(self.symbols, self.regions, 90)
        self.assertTrue(any("scanout" in error for error in errors))

        self.symbols["graphics_scanout_storage"] = 0x262C0000
        self.assertEqual(
            check_elf.validate_layout(self.symbols, self.regions, 90), []
        )

    def test_rejects_invalid_static_thread_layout(self):
        self.symbols["__sdlpal_thread_end__"] = 0x26065000
        self.symbols["__HeapBase"] = 0x26064000
        errors = check_elf.validate_layout(self.symbols, self.regions, 0)
        self.assertTrue(any("smaller than 24 KiB" in error for error in errors))
        self.assertTrue(any("heap overlaps" in error for error in errors))

        self.symbols["__sdlpal_thread_end__"] = 0x261C0001
        self.symbols["__HeapBase"] = 0x261C0001
        errors = check_elf.validate_layout(self.symbols, self.regions, 0)
        self.assertTrue(any("outside Secondary SRAM" in error for error in errors))

    def test_rejects_invalid_indexed_staging_layout(self):
        del self.symbols["__lcd_indexed_staging_end__"]
        errors = check_elf.validate_layout(self.symbols, self.regions, 0)
        self.assertTrue(any("staging" in error for error in errors))

        self.symbols["__lcd_indexed_staging_end__"] = 0x26309601
        errors = check_elf.validate_layout(self.symbols, self.regions, 0)
        self.assertTrue(any("64000" in error for error in errors))

        self.symbols["__lcd_indexed_staging_start__"] = 0x261FFFFF
        self.symbols["__lcd_indexed_staging_end__"] = 0x2620F9FF
        errors = check_elf.validate_layout(self.symbols, self.regions, 0)
        self.assertTrue(any("outside GFX" in error for error in errors))

    def test_rejects_missing_pal_large_storage_layout(self):
        del self.symbols["__sdlpal_large_end__"]
        errors = check_elf.validate_layout(self.symbols, self.regions, 0)
        self.assertTrue(any("__sdlpal_large_end__" in error for error in errors))

    def test_rejects_out_of_budget_pal_large_storage(self):
        start = self.symbols["__sdlpal_large_start__"]
        self.symbols["__sdlpal_large_end__"] = (
            start + check_elf.PAL_LARGE_MIN_BYTES - 1
        )
        errors = check_elf.validate_layout(self.symbols, self.regions, 0)
        self.assertTrue(any("PAL_LARGE" in error for error in errors))

        self.symbols["__sdlpal_large_end__"] = (
            start + check_elf.PAL_LARGE_MAX_BYTES + 1
        )
        self.symbols["__cy_gpu_buf_end__"] = self.symbols["__sdlpal_large_end__"]
        errors = check_elf.validate_layout(self.symbols, self.regions, 0)
        self.assertTrue(any("PAL_LARGE" in error for error in errors))

    def test_rejects_missing_save_reserve_layout(self):
        del self.symbols["__sdlpal_save_end__"]
        errors = check_elf.validate_layout(self.symbols, self.regions, 0)
        self.assertTrue(any("__sdlpal_save_end__" in error for error in errors))

    def test_rejects_wrong_save_reserve_size(self):
        self.symbols["__sdlpal_save_end__"] = (
            self.symbols["__sdlpal_save_start__"] + 196607
        )
        errors = check_elf.validate_layout(self.symbols, self.regions, 0)
        self.assertTrue(any("196608" in error for error in errors))

    def test_rejects_save_reserve_outside_gfx(self):
        self.symbols["__sdlpal_save_start__"] = 0x264F0000
        self.symbols["__sdlpal_save_end__"] = 0x26520000
        errors = check_elf.validate_layout(self.symbols, self.regions, 0)
        self.assertTrue(any("save reserve" in error for error in errors))

    def test_portrait_rejects_scanout_buffer(self):
        self.symbols["graphics_scanout_storage"] = 0x262C0000
        errors = check_elf.validate_layout(self.symbols, self.regions, 180)
        self.assertTrue(any("scanout" in error for error in errors))

    def test_rejects_missing_or_outside_itcm_hot_code(self):
        del self.symbols["PAL_StartFrame"]
        errors = check_elf.validate_layout(self.symbols, self.regions, 0)
        self.assertTrue(any("PAL_StartFrame" in error for error in errors))

        self.symbols["PAL_StartFrame"] = 0x00000800
        errors = check_elf.validate_layout(self.symbols, self.regions, 0)
        self.assertTrue(any("outside ITCM" in error for error in errors))

        self.symbols["PAL_StartFrame"] = 0x60801000
        errors = check_elf.validate_layout(self.symbols, self.regions, 0)
        self.assertTrue(any("outside ITCM" in error for error in errors))

    def test_rejects_itcm_reserve_violation(self):
        self.symbols["__ram_vectors_end__"] = 0x00030001
        errors = check_elf.validate_layout(self.symbols, self.regions, 0)
        self.assertTrue(any("64 KiB" in error for error in errors))

    def test_parses_split_map_section_header(self):
        text = """Memory Configuration
Name Origin Length Attributes
m55_data_INTERNAL 0x20000000 0x00040000
gfx_mem 0x26200000 0x00300000
Linker script and memory map
.pal_framebuffer
                0x20000000    0x20000
.cy_gpu_buf     0x26200000    0xf9c00
"""
        regions, sections = check_elf.parse_map(text)
        self.assertEqual(regions["gfx_mem"], (0x26200000, 0x300000))
        self.assertEqual(
            sections["pal_framebuffer"], (0x20000000, 0x20000)
        )


if __name__ == "__main__":
    unittest.main()
