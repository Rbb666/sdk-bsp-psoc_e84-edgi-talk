HOST_CC ?= gcc
CFLAGS ?= -std=c99 -Wall -Wextra -Werror -pedantic
PLATFORM := ../../platform
APPLICATIONS := ../../applications
TOUCH_DRIVER := ../../../../libraries/Common/board/ports/display_panels/drv_touch.c
INCLUDES := -I$(PLATFORM)
SDL_SHIM := ../../sdlpal/upstream/esp32s3/native_engine_shim
SDL_INCLUDES := -I$(SDL_SHIM)/include
FAKE_INCLUDES := -Ifakes
TOUCH_FAKE_INCLUDES := -Ifakes_touch
DISPLAY_FAKE_INCLUDES := -Ifakes_display

.PHONY: all test_display_core test_display_port test_touch_core test_port_headers test_storage \
	test_sdl_shim test_sdl_shim_surface_fallback test_libc_compat test_engine_bridge test_boot_state \
	test_memory_policy test_engine_heap test_save_io_core test_touch_driver \
	test_touch_poll_cache test_audio_cache test_voc_mixer test_audio_queue \
	test_rix_output clean

all: test_display_core test_display_port test_touch_core test_port_headers test_storage \
	test_sdl_shim test_libc_compat test_engine_bridge test_boot_state \
	test_memory_policy test_engine_heap test_save_io_core test_touch_driver \
	test_touch_poll_cache test_surface_storage \
	test_sdl_shim_surface_fallback

test_display_core: test_display_core.exe
	.\test_display_core.exe

test_display_core.exe: test_display_core.c $(PLATFORM)/pal_display_core.c \
		$(PLATFORM)/pal_display_core.h $(PLATFORM)/pal_controls.c \
		$(PLATFORM)/pal_controls.h
	$(HOST_CC) $(CFLAGS) $(INCLUDES) test_display_core.c \
		$(PLATFORM)/pal_display_core.c $(PLATFORM)/pal_controls.c -o $@

test_display_port: test_display_port.exe
	.\test_display_port.exe

test_display_port.exe: test_display_port.c $(PLATFORM)/pal_display_port.c \
		$(PLATFORM)/pal_display_port.h $(PLATFORM)/pal_display_core.c \
		$(PLATFORM)/pal_display_core.h $(PLATFORM)/pal_controls.c \
		$(PLATFORM)/pal_controls.h $(PLATFORM)/pal_lcd_api.h \
		fakes_display/rtthread.h
	$(HOST_CC) $(CFLAGS) $(DISPLAY_FAKE_INCLUDES) $(INCLUDES) \
		-DBSP_LCD_VGLITE_INDEXED test_display_port.c \
		$(PLATFORM)/pal_display_port.c $(PLATFORM)/pal_display_core.c \
		$(PLATFORM)/pal_controls.c -o $@

test_touch_core: test_touch_core.exe
	.\test_touch_core.exe

test_touch_core.exe: test_touch_core.c $(PLATFORM)/pal_touch_core.c \
		$(PLATFORM)/pal_touch_core.h $(PLATFORM)/pal_controls.c \
		$(PLATFORM)/pal_controls.h
	$(HOST_CC) $(CFLAGS) $(INCLUDES) test_touch_core.c \
		$(PLATFORM)/pal_touch_core.c $(PLATFORM)/pal_controls.c -o $@

test_touch_poll_cache: test_touch_poll_cache.exe
	.\test_touch_poll_cache.exe

test_touch_poll_cache.exe: test_touch_poll_cache.c \
		$(PLATFORM)/pal_touch_poll_cache.c \
		$(PLATFORM)/pal_touch_poll_cache.h $(PLATFORM)/pal_touch_core.h
	$(HOST_CC) $(CFLAGS) $(INCLUDES) test_touch_poll_cache.c \
		$(PLATFORM)/pal_touch_poll_cache.c -o $@

test_port_headers: test_port_headers.c $(PLATFORM)/pal_display_port.h \
		$(PLATFORM)/pal_touch_port.h $(PLATFORM)/pal_status.h
	$(HOST_CC) $(CFLAGS) $(INCLUDES) -fsyntax-only test_port_headers.c

test_storage: test_storage.exe
	.\test_storage.exe

test_storage.exe: test_storage.c $(PLATFORM)/pal_storage.c \
		$(PLATFORM)/pal_storage.h
	$(HOST_CC) $(CFLAGS) $(INCLUDES) test_storage.c \
		$(PLATFORM)/pal_storage.c -o $@

test_sdl_shim: test_sdl_shim.exe
	.\test_sdl_shim.exe

test_sdl_shim.exe: test_sdl_shim.c $(SDL_SHIM)/sdl_shim.c \
		$(SDL_SHIM)/include/SDL.h
	$(HOST_CC) $(CFLAGS) $(SDL_INCLUDES) \
		-DPAL_ENGINE_BRIDGE_REQUIRE_TARGET_HOOKS=0 \
		-DPAL_SDL_SHIM_EXTERNAL_SURFACES_ONLY=1 \
		-DPAL_SDL_SHIM_DYNAMIC_SURFACES=1 \
	test_sdl_shim.c $(SDL_SHIM)/sdl_shim.c -o $@

test_sdl_shim_surface_fallback: test_sdl_shim_surface_fallback.exe
	.\test_sdl_shim_surface_fallback.exe

test_sdl_shim_surface_fallback.exe: test_sdl_shim_surface_fallback.c \
		$(SDL_SHIM)/sdl_shim.c $(SDL_SHIM)/include/SDL.h \
		$(PLATFORM)/pal_surface_storage.c \
		$(PLATFORM)/pal_surface_storage.h \
		$(PLATFORM)/pal_memory_policy.c \
		$(PLATFORM)/pal_memory_policy.h
	$(HOST_CC) $(CFLAGS) $(INCLUDES) $(SDL_INCLUDES) \
		-DPAL_ENGINE_BRIDGE_REQUIRE_TARGET_HOOKS=0 \
		-DPAL_SDL_SHIM_EXTERNAL_SURFACES_ONLY=1 \
		-DPAL_SDL_SHIM_DYNAMIC_SURFACES=1 \
		-DPAL_PSOC_DIRECT_INDEXED=1 \
		test_sdl_shim_surface_fallback.c $(SDL_SHIM)/sdl_shim.c \
		$(PLATFORM)/pal_surface_storage.c \
		$(PLATFORM)/pal_memory_policy.c -o $@

test_libc_compat: test_libc_compat.exe
	.\test_libc_compat.exe

test_libc_compat.exe: test_libc_compat.c $(PLATFORM)/pal_libc_compat.c \
		$(PLATFORM)/pal_libc_compat.h
	$(HOST_CC) $(CFLAGS) $(INCLUDES) test_libc_compat.c \
		$(PLATFORM)/pal_libc_compat.c -o $@

test_engine_bridge: test_engine_bridge.exe
	.\test_engine_bridge.exe

test_engine_bridge.exe: test_engine_bridge.c \
		$(PLATFORM)/pal_engine_bridge.c $(PLATFORM)/pal_engine_bridge.h
	$(HOST_CC) $(CFLAGS) $(FAKE_INCLUDES) $(INCLUDES) $(SDL_INCLUDES) \
		test_engine_bridge.c $(PLATFORM)/pal_engine_bridge.c -o $@

test_boot_state: test_boot_state.exe
	.\test_boot_state.exe

test_boot_state.exe: test_boot_state.c $(APPLICATIONS)/pal_boot_state.c \
		$(APPLICATIONS)/pal_boot_state.h $(PLATFORM)/pal_storage.h
	$(HOST_CC) $(CFLAGS) $(INCLUDES) -I$(APPLICATIONS) test_boot_state.c \
		$(APPLICATIONS)/pal_boot_state.c -o $@

test_memory_policy: test_memory_policy.exe
	.\test_memory_policy.exe

test_memory_policy.exe: test_memory_policy.c \
		$(PLATFORM)/pal_memory_policy.c $(PLATFORM)/pal_memory_policy.h
	$(HOST_CC) $(CFLAGS) $(INCLUDES) test_memory_policy.c \
		$(PLATFORM)/pal_memory_policy.c -o $@

test_engine_heap: test_engine_heap.exe
	.\test_engine_heap.exe

test_engine_heap.exe: test_engine_heap.c \
		$(PLATFORM)/pal_engine_heap.c $(PLATFORM)/pal_engine_heap.h \
		$(PLATFORM)/pal_memory_policy.c $(PLATFORM)/pal_memory_policy.h
	$(HOST_CC) $(CFLAGS) $(INCLUDES) test_engine_heap.c \
		$(PLATFORM)/pal_engine_heap.c \
		$(PLATFORM)/pal_memory_policy.c -o $@

test_surface_storage: test_surface_storage.exe
	.\test_surface_storage.exe

test_surface_storage.exe: test_surface_storage.c \
		$(PLATFORM)/pal_surface_storage.c \
		$(PLATFORM)/pal_surface_storage.h \
		$(PLATFORM)/pal_memory_policy.c \
		$(PLATFORM)/pal_memory_policy.h
	$(HOST_CC) $(CFLAGS) $(INCLUDES) test_surface_storage.c \
		$(PLATFORM)/pal_surface_storage.c \
		$(PLATFORM)/pal_memory_policy.c -o $@

test_save_io_core: test_save_io_core.exe
	.\test_save_io_core.exe

test_save_io_core.exe: test_save_io_core.c \
		$(PLATFORM)/pal_save_io_core.c $(PLATFORM)/pal_save_io_core.h
	$(HOST_CC) $(CFLAGS) $(INCLUDES) test_save_io_core.c \
		$(PLATFORM)/pal_save_io_core.c -o $@

test_touch_driver: test_touch_driver.exe
	.\test_touch_driver.exe

test_touch_driver.exe: test_touch_driver.c $(TOUCH_DRIVER)
	$(HOST_CC) $(CFLAGS) -ffunction-sections -fdata-sections \
		-Wno-unused-parameter $(TOUCH_FAKE_INCLUDES) \
		-DBSP_USING_SDLPAL -DST7102_HOST_TEST test_touch_driver.c \
		$(TOUCH_DRIVER) -Wl,--gc-sections -o $@

clean:
	-del /q *.exe 2>nul
