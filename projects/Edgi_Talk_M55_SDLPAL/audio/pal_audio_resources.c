#include "pal_audio_resources.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

extern int PAL_MKFGetChunkCount(FILE *file);
extern int PAL_MKFGetChunkSize(unsigned int chunk, FILE *file);
extern int PAL_MKFReadChunk(unsigned char *buffer,
                            unsigned int buffer_size,
                            unsigned int chunk, FILE *file);

#define PAL_AUDIO_RESOURCE_PATH_BYTES 256u

typedef union pal_audio_allocation_header
{
    struct
    {
        size_t bytes;
    } allocation;
    long double align_long_double;
    void *align_pointer;
    uint64_t align_u64;
} pal_audio_allocation_header_t;

static FILE *resource_file(pal_audio_resources_t *resources,
                           uint8_t archive)
{
    if (resources == NULL)
    {
        return NULL;
    }
    if (archive == PAL_AUDIO_ARCHIVE_MUSIC)
    {
        return (FILE *)resources->music_file;
    }
    if (archive == PAL_AUDIO_ARCHIVE_SOUND)
    {
        return (FILE *)resources->sound_file;
    }
    return NULL;
}

static FILE *open_archive(const char *root, const char *name)
{
    char path[PAL_AUDIO_RESOURCE_PATH_BYTES];
    const char *separator;
    size_t root_length;
    int length;

    if (root == NULL || root[0] == '\0')
    {
        return NULL;
    }
    root_length = strlen(root);
    separator = root[root_length - 1u] == '/' ||
                        root[root_length - 1u] == '\\'
                    ? ""
                    : "/";
    length = snprintf(path, sizeof(path), "%s%s%s", root, separator, name);
    if (length < 0 || (size_t)length >= sizeof(path))
    {
        return NULL;
    }
    return fopen(path, "rb");
}

static int cache_get_size(void *context, uint8_t archive,
                          uint16_t chunk, size_t *size)
{
    FILE *file = resource_file((pal_audio_resources_t *)context, archive);
    int count;
    int bytes;

    if (file == NULL || size == NULL)
    {
        return 0;
    }
    count = PAL_MKFGetChunkCount(file);
    if (count <= 0 || chunk >= (uint16_t)count)
    {
        return 0;
    }
    bytes = PAL_MKFGetChunkSize(chunk, file);
    if (bytes <= 0)
    {
        return 0;
    }
    *size = (size_t)bytes;
    return 1;
}

static int cache_load(void *context, uint8_t archive, uint16_t chunk,
                      void *destination, size_t size)
{
    FILE *file = resource_file((pal_audio_resources_t *)context, archive);
    int result;

    if (file == NULL || destination == NULL || size == 0u || size > UINT_MAX)
    {
        return 0;
    }
    result = PAL_MKFReadChunk((unsigned char *)destination,
                              (unsigned int)size, chunk, file);
    return result == (int)size;
}

static void *cache_alloc(void *context, size_t size)
{
    pal_audio_allocation_header_t *header;
    size_t total;

    (void)context;
    if (size == 0u || size > SIZE_MAX - sizeof(*header))
    {
        return NULL;
    }
    total = sizeof(*header) + size;
    header = (pal_audio_allocation_header_t *)pal_cold_alloc(
        total, PAL_MEMORY_TAG_RESOURCE);
    if (header == NULL)
    {
        return NULL;
    }
    header->allocation.bytes = total;
    return header + 1;
}

static void cache_free(void *context, void *pointer)
{
    pal_audio_allocation_header_t *header;

    (void)context;
    if (pointer == NULL)
    {
        return;
    }
    header = (pal_audio_allocation_header_t *)pointer - 1;
    pal_cold_free(header, header->allocation.bytes,
                  PAL_MEMORY_TAG_RESOURCE);
}

void pal_audio_resources_init(pal_audio_resources_t *resources)
{
    if (resources != NULL)
    {
        memset(resources, 0, sizeof(*resources));
    }
}

int pal_audio_resources_open(pal_audio_resources_t *resources,
                             const char *root)
{
    if (resources == NULL)
    {
        return 0;
    }
    resources->music_file = open_archive(root, "mus.mkf");
    resources->sound_file = open_archive(root, "voc.mkf");
    return resources->music_file != NULL || resources->sound_file != NULL;
}

void pal_audio_resources_close(pal_audio_resources_t *resources)
{
    if (resources == NULL)
    {
        return;
    }
    if (resources->music_file != NULL)
    {
        (void)fclose((FILE *)resources->music_file);
    }
    if (resources->sound_file != NULL)
    {
        (void)fclose((FILE *)resources->sound_file);
    }
    pal_audio_resources_init(resources);
}

int pal_audio_resources_music_available(
    const pal_audio_resources_t *resources)
{
    return resources != NULL && resources->music_file != NULL;
}

int pal_audio_resources_sound_available(
    const pal_audio_resources_t *resources)
{
    return resources != NULL && resources->sound_file != NULL;
}

void pal_audio_resources_cache_init(pal_audio_resources_t *resources,
                                    pal_audio_cache_t *cache,
                                    size_t limit)
{
    pal_audio_cache_init(cache, limit, cache_get_size, cache_load,
                         resources, cache_alloc, cache_free, resources);
}
