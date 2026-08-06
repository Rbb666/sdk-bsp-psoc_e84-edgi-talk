#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#define test_mkdir(path) _mkdir(path)
#define test_rmdir(path) _rmdir(path)
#else
#include <sys/stat.h>
#include <unistd.h>
#define test_mkdir(path) mkdir((path), 0777)
#define test_rmdir(path) rmdir(path)
#endif

#include "pal_audio_resources.h"

static size_t cold_bytes;

void *pal_cold_alloc(size_t size, pal_memory_tag_t tag)
{
    (void)tag;
    cold_bytes += size;
    return malloc(size);
}

void pal_cold_free(void *pointer, size_t size, pal_memory_tag_t tag)
{
    (void)tag;
    assert(cold_bytes >= size);
    cold_bytes -= size;
    free(pointer);
}

int PAL_MKFGetChunkCount(FILE *fp)
{
    return fp != NULL ? 1 : 0;
}

int PAL_MKFGetChunkSize(unsigned int chunk, FILE *fp)
{
    long size;

    if (chunk != 0u || fp == NULL || fseek(fp, 0, SEEK_END) != 0)
    {
        return -1;
    }
    size = ftell(fp);
    (void)fseek(fp, 0, SEEK_SET);
    return size >= 0 && size <= INT32_MAX ? (int)size : -1;
}

int PAL_MKFReadChunk(unsigned char *buffer, unsigned int buffer_size,
                     unsigned int chunk, FILE *fp)
{
    int size = PAL_MKFGetChunkSize(chunk, fp);

    if (size < 0 || (unsigned int)size > buffer_size)
    {
        return -1;
    }
    return fread(buffer, 1u, (size_t)size, fp) == (size_t)size ? size : -1;
}

static void write_fixture(const char *path, uint8_t seed)
{
    uint8_t bytes[4] = {seed, (uint8_t)(seed + 1u),
                        (uint8_t)(seed + 2u), (uint8_t)(seed + 3u)};
    FILE *file = fopen(path, "wb");

    assert(file != NULL);
    assert(fwrite(bytes, 1u, sizeof(bytes), file) == sizeof(bytes));
    assert(fclose(file) == 0);
}

int main(void)
{
    const char *directory = "audio_resource_fixture";
    const char *music_path = "audio_resource_fixture/mus.mkf";
    const char *sound_path = "audio_resource_fixture/voc.mkf";
    pal_audio_resources_t resources;
    pal_audio_cache_t cache;
    pal_audio_cache_handle_t handle;

    (void)test_mkdir(directory);
    write_fixture(music_path, 0x10u);
    write_fixture(sound_path, 0x20u);

    pal_audio_resources_init(&resources);
    assert(pal_audio_resources_open(&resources, directory));
    assert(pal_audio_resources_music_available(&resources));
    assert(pal_audio_resources_sound_available(&resources));
    pal_audio_resources_cache_init(&resources, &cache, 64u);

    assert(pal_audio_cache_acquire(
        &cache, PAL_AUDIO_ARCHIVE_MUSIC, 0u, &handle));
    assert(handle.size == 4u && handle.data[0] == 0x10u);
    pal_audio_cache_release(&cache, &handle);
    assert(pal_audio_cache_acquire(
        &cache, PAL_AUDIO_ARCHIVE_SOUND, 0u, &handle));
    assert(handle.size == 4u && handle.data[0] == 0x20u);
    pal_audio_cache_release(&cache, &handle);
    pal_audio_cache_clear(&cache);
    assert(cold_bytes == 0u);
    pal_audio_resources_close(&resources);

    assert(remove(sound_path) == 0);
    pal_audio_resources_init(&resources);
    assert(pal_audio_resources_open(&resources, directory));
    assert(pal_audio_resources_music_available(&resources));
    assert(!pal_audio_resources_sound_available(&resources));
    pal_audio_resources_close(&resources);

    assert(remove(music_path) == 0);
    assert(test_rmdir(directory) == 0);
    puts("audio_resources: PASS");
    return 0;
}
