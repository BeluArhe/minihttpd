#ifndef FILES_H
#define FILES_H

#include <stddef.h>
#include <sys/stat.h>

typedef enum {
    FILE_OK            = 0,
    FILE_ERR_FORBIDDEN = -403,
    FILE_ERR_NOT_FOUND = -404,
    FILE_ERR_INTERNAL  = -500
} file_status_t;

/*
 * root_dir debe ser la ruta canónica ya resuelta con realpath() al arrancar.
 */
file_status_t file_open_safe(const char *resolved_root,
                             const char *uri,
                             int *out_fd,
                             struct stat *out_stat,
                             char *resolved_path,
                             size_t resolved_path_size);

#endif /* FILES_H */
