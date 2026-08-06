#ifndef PAL_ENGINE_IO_HOOKS_H
#define PAL_ENGINE_IO_HOOKS_H

#include "pal_engine_io.h"

#define malloc pal_engine_malloc
#define calloc pal_engine_calloc
#define realloc pal_engine_realloc
#define free pal_engine_free
#define fopen pal_engine_fopen
#define fwrite pal_engine_fwrite
#define fclose pal_engine_fclose

#endif
