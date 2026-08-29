#ifndef RETRO_GO_CORE_H
#define RETRO_GO_CORE_H

#include <stdbool.h>

/* Run one bounded host frame. Returns true when a video frame was submitted. */
bool retro_go_core_run_frame(bool draw);

#endif
