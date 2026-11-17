#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>
#include "files.h"

file_status_t file_open_safe(const char *resolved_root,
                             const char *uri,
                             int *out_fd,
                             struct stat *out_stat,
                             char *resolved_path,
                             size_t resolved_path_size) {
    if (!resolved_root || !uri || !out_fd || !out_stat || !resolved_path) {
        return FILE_ERR_INTERNAL;
    }

    *out_fd = -1;

    char raw_path[PATH_MAX];
    int n;
    if (strcmp(uri, "/") == 0) {
        n = snprintf(raw_path, sizeof(raw_path), "%s/index.html", resolved_root);
    } else {
        n = snprintf(raw_path, sizeof(raw_path), "%s%s", resolved_root, uri);
    }

    if (n < 0 || (size_t)n >= sizeof(raw_path)) {
        return FILE_ERR_NOT_FOUND;
    }

    char canonical_file[PATH_MAX];
    if (realpath(raw_path, canonical_file) == NULL) {
        /*
         * Incluimos EACCES y EPERM en 404 porque antes de resolver realpath
         * no sabemos si la ruta escapó de resolved_root; devolver 403 aquí
         * filtraría la existencia de directorios restringidos del sistema.
         */
        if (errno == ENOENT || errno == ENOTDIR || errno == ELOOP ||
            errno == ENAMETOOLONG || errno == EACCES || errno == EPERM) {
            return FILE_ERR_NOT_FOUND;
        }
        return FILE_ERR_INTERNAL; /* EMFILE, ENFILE, ENOMEM, EIO -> 500 */
    }

    /* Protección Path Traversal: fuera de resolved_root -> 404 uniforme */
    size_t root_len = strlen(resolved_root);
    if (strncmp(canonical_file, resolved_root, root_len) != 0 ||
        (canonical_file[root_len] != '\0' && canonical_file[root_len] != '/')) {
        return FILE_ERR_NOT_FOUND;
    }

    size_t canonical_len = strlen(canonical_file);
    if (canonical_len >= resolved_path_size) {
        return FILE_ERR_INTERNAL;
    }
    memcpy(resolved_path, canonical_file, canonical_len + 1);

    /* Ya comprobado que está dentro de resolved_root: EACCES aquí sí es 403 legítimo */
    int fd = open(canonical_file, O_RDONLY);
    if (fd < 0) {
        if (errno == EACCES || errno == EPERM) {
            return FILE_ERR_FORBIDDEN;
        }
        if (errno == ENOENT || errno == ENOTDIR || errno == ELOOP) {
            return FILE_ERR_NOT_FOUND;
        }
        return FILE_ERR_INTERNAL;
    }

    if (fstat(fd, out_stat) < 0) {
        close(fd);
        return FILE_ERR_INTERNAL;
    }

    if (!S_ISREG(out_stat->st_mode)) {
        close(fd);
        return FILE_ERR_FORBIDDEN;
    }

    *out_fd = fd;
    return FILE_OK;
}
