import os

ARCH = 'arm'
CPU = 'cortex-m55'
CROSS_TOOL = 'gcc'
PLATFORM = 'gcc'
BSP_LIBRARY_TYPE = None
EXEC_PATH = os.environ.get('RTT_EXEC_PATH', '')

PREFIX = 'arm-none-eabi-'
CC = PREFIX + 'gcc'
AS = CC
AR = PREFIX + 'ar'
CXX = PREFIX + 'g++'
LINK = CXX
TARGET_EXT = 'elf'
SIZE = PREFIX + 'size'
OBJDUMP = PREFIX + 'objdump'
OBJCPY = PREFIX + 'objcopy'

DEVICE = ('-mcpu=cortex-m55 -mthumb -mfpu=auto -mfloat-abi=hard --specs=nano.specs '
          '-ffunction-sections -fdata-sections -nostartfiles')
CFLAGS = DEVICE + ' -O2 -g -Wall'
CXXFLAGS = CFLAGS + ' -std=gnu++11 -fno-exceptions -fno-rtti'
AFLAGS = '-c ' + DEVICE + ' -x assembler-with-cpp -Wa,-mimplicit-it=thumb -g'
LFLAGS = (DEVICE + ' -Wl,--gc-sections,-Map=rtthread.map,-cref,-u,Reset_Handler '
          '-Wl,--wrap=vg_lite_hal_wait_interrupt,--wrap=vg_lite_kernel '
          '-T platform/linker_scripts/link.ld')
POST_ACTION = OBJCPY + ' -O ihex $TARGET rtthread.hex\n' + SIZE + ' $TARGET\n'
