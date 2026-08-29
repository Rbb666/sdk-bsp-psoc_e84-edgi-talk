#ifndef RETRO_GO_GPSP_PRELUDE_H
#define RETRO_GO_GPSP_PRELUDE_H

/* common.h normally includes the upstream sound.h itself. Suppress that one
 * include, finish the common type declarations, then install the platform's
 * 32 kHz-compatible sound interface for all gpSP translation units. */
#define SOUND_H
#include "common.h"
#undef SOUND_H
#include "retro_go_gpsp_sound.h"

#endif
