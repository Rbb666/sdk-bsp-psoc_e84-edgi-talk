#include "pal_storage.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#endif

#define PAL_STORAGE_PATH_CAPACITY 256u

static const char *const required_files[] = {
    "abc.mkf", "ball.mkf", "data.mkf", "f.mkf", "fbp.mkf",
    "fire.mkf", "gop.mkf", "map.mkf", "mgo.mkf", "pat.mkf",
    "rgm.mkf", "rng.mkf", "sss.mkf", "word.dat", "m.msg",
};

static bool default_probe(const char *path, void *context)
{
    struct stat status;
    (void)context;
    return path != NULL && stat(path, &status) == 0;
}

pal_storage_result_t pal_storage_validate(const char *root,
                                          pal_storage_probe_fn probe,
                                          void *context)
{
    pal_storage_result_t result = {false, {0}};
    size_t root_length;
    const char *separator;
    size_t i;

    if (root == NULL || root[0] == '\0')
    {
        return result;
    }
    if (probe == NULL)
    {
        probe = default_probe;
    }

    root_length = strlen(root);
    separator = root[root_length - 1u] == '/' ||
                        root[root_length - 1u] == '\\'
                    ? ""
                    : "/";

    for (i = 0u; i < sizeof(required_files) / sizeof(required_files[0]); ++i)
    {
        char path[PAL_STORAGE_PATH_CAPACITY];
        int length = snprintf(path, sizeof(path), "%s%s%s", root, separator,
                              required_files[i]);

        if (length < 0 || (size_t)length >= sizeof(path) ||
            !probe(path, context))
        {
            (void)snprintf(result.missing_name, sizeof(result.missing_name),
                           "%s", required_files[i]);
            return result;
        }
    }

    result.ready = true;
    return result;
}

bool pal_storage_path_is_directory(const char *path)
{
    struct stat status;
    return path != NULL && stat(path, &status) == 0 &&
           S_ISDIR(status.st_mode);
}

bool pal_storage_prepare_save_dir(const char *path)
{
    struct stat status;
    int create_result;

    if (path == NULL || path[0] == '\0')
    {
        return false;
    }

    if (stat(path, &status) == 0)
    {
        return S_ISDIR(status.st_mode);
    }

#if defined(_WIN32)
    create_result = _mkdir(path);
#else
    create_result = mkdir(path, 0777);
#endif
    if (create_result == 0)
    {
        return true;
    }

    return errno == EEXIST && pal_storage_path_is_directory(path);
}
