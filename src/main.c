#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include "server.h"

int passiveTCP(const char *service, int qlen);
int errexit(const char *format, ...);

int main(int argc, char *argv[]) {
    const char *service = (argc > 1) ? argv[1] : "8080";
    char resolved_root[PATH_MAX];

    if (realpath("www", resolved_root) == NULL) {
        errexit("No se pudo resolver el directorio raiz www\n");
    }

    int sd = passiveTCP(service, 32);
    printf("Servidor MiniHTTPd escuchando en el puerto %s (root: %s)...\n",
           service, resolved_root);

    server_run(sd, resolved_root);
    return 0;
}
