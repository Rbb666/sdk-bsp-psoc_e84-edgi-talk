#ifndef PAL_ENGINE_IO_H
#define PAL_ENGINE_IO_H

#include <stddef.h>
#include <stdio.h>

void *pal_engine_malloc(size_t size);
void pal_engine_free(void *pointer);
FILE *pal_engine_fopen(const char *path, const char *mode);
size_t pal_engine_fwrite(const void *data, size_t element_size,
                         size_t element_count, FILE *stream);
int pal_engine_fclose(FILE *stream);

#endif
