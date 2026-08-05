#include "aviplay.h"
#include "players.h"

#include <string.h>

LPAUDIOPLAYER RIX_Init(LPCSTR file_name)
{
    (void)file_name;
    return NULL;
}

LPAUDIOPLAYER OGG_Init(void)
{
    return NULL;
}

LPAUDIOPLAYER OPUS_Init(void)
{
    return NULL;
}

LPAUDIOPLAYER MP3_Init(void)
{
    return NULL;
}

LPAUDIOPLAYER TIMIDITY_Init(void)
{
    return NULL;
}

LPAUDIOPLAYER TSF_Init(void)
{
    return NULL;
}

LPAUDIOPLAYER SOUND_Init(void)
{
    return NULL;
}

void PAL_AVIInit(void)
{
}

void PAL_AVIShutdown(void)
{
}

BOOL PAL_PlayAVI(const char *path)
{
    (void)path;
    return FALSE;
}

void SDLCALL AVI_FillAudioBuffer(void *userdata, uint8_t *stream, int length)
{
    (void)userdata;
    if (stream != NULL && length > 0) {
        memset(stream, 0, (size_t)length);
    }
}

void *AVI_GetPlayState(void)
{
    return NULL;
}
