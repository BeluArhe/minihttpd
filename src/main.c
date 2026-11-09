#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "mime.h"

int passiveTCP(const char *service, int qlen);
int errexit(const char *format, ...);

#define BUFSIZE 4096

// Función centralizada de errores con validación estricta de límites (evita buffer over-read / info leakage)
void enviar_respuesta_error(int nsd, int status_code, const char *status_text) {
    char body[256];
    int body_len = snprintf(body, sizeof(body), "<h1>%d %s</h1>", status_code, status_text);
    if (body_len < 0 || (size_t)body_len >= sizeof(body)) {
        return; // Cuerpo demasiado largo para el buffer seguro
    }

    char header[512];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        status_code, status_text, body_len, body);

    // Validación de truncamiento para evitar lectura fuera de límites de la pila
    if (header_len < 0 || (size_t)header_len >= sizeof(header)) {
        return; 
    }

    send(nsd, header, header_len, MSG_NOSIGNAL);
}

int main(int argc, char *argv[]) {
    char *service = (argc > 1) ? argv[1] : "8080";

    struct sockaddr_in addrclt;
    socklen_t addrSZ;
    char buf[BUFSIZE];
    int sd, nsd;
    int nread;

    sd = passiveTCP(service, 32);
    printf("Servidor MiniHTTPd escuchando en el puerto %s...\n", service);

    while (1) {
        addrSZ = sizeof(addrclt);
        nsd = accept(sd, (struct sockaddr *)&addrclt, &addrSZ);
        if (nsd < 0) {
            perror("Error en accept");
            continue;
        }

        nread = read(nsd, buf, BUFSIZE - 1);
        if (nread > 0) {
            buf[nread] = '\0';

            // 1. Aislar estrictamente la primera línea de la petición HTTP (buscar \r\n)
            char *line_end = strstr(buf, "\r\n");
            if (!line_end) {
                // Si no hay \r\n, la petición está malformada (rechaza saltos de línea sueltos)
                enviar_respuesta_error(nsd, 400, "Bad Request");
                close(nsd);
                continue;
            }
            *line_end = '\0'; // Cortar el buffer temporalmente para aislar la Request-Line

            char method[16], url[256], protocol[16];
            int parsed = sscanf(buf, "%15s %255s %15s", method, url, protocol);

            // 2. Validar parseo de exactamente 3 campos sobre la línea aislada
            if (parsed != 3 || strlen(url) >= 255) {
                enviar_respuesta_error(nsd, 400, "Bad Request");
                close(nsd);
                continue;
            }

            if (strcmp(method, "GET") != 0) {
                enviar_respuesta_error(nsd, 405, "Method Not Allowed");
                close(nsd);
                continue;
            }

            char raw_path[512];
            if (strcmp(url, "/") == 0) {
                snprintf(raw_path, sizeof(raw_path), "www/index.html");
            } else {
                snprintf(raw_path, sizeof(raw_path), "www%s", url);
            }

            char resolved_www[PATH_MAX];
            char resolved_file[PATH_MAX];

            if (realpath("www", resolved_www) == NULL) {
                enviar_respuesta_error(nsd, 500, "Internal Server Error");
                close(nsd);
                continue;
            }

            if (realpath(raw_path, resolved_file) == NULL) {
                enviar_respuesta_error(nsd, 404, "Not Found");
                close(nsd);
                continue;
            }

            // Verificación estricta de subdirectorio (Path Traversal protection)
            size_t www_len = strlen(resolved_www);
            if (strncmp(resolved_file, resolved_www, www_len) != 0 ||
                (resolved_file[www_len] != '\0' && resolved_file[www_len] != '/')) {
                enviar_respuesta_error(nsd, 403, "Forbidden");
                close(nsd);
                continue;
            }

            int fd = open(resolved_file, O_RDONLY);
            if (fd < 0) {
                enviar_respuesta_error(nsd, 404, "Not Found");
            } else {
                struct stat st;
                if (fstat(fd, &st) < 0) {
                    close(fd);
                    enviar_respuesta_error(nsd, 500, "Internal Server Error");
                } else if (!S_ISREG(st.st_mode)) {
                    close(fd);
                    enviar_respuesta_error(nsd, 403, "Forbidden");
                } else {
                    const char *mime_type = get_mime_type(resolved_file);

                    char header[512];
                    int header_len = snprintf(header, sizeof(header),
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: %s\r\n"
                        "X-Content-Type-Options: nosniff\r\n"
                        "Content-Length: %lld\r\n"
                        "\r\n",
                        mime_type, (long long)st.st_size);

                    if (header_len < 0 || (size_t)header_len >= sizeof(header)) {
                        close(fd);
                        enviar_respuesta_error(nsd, 500, "Internal Server Error");
                        close(nsd);
                        continue;
                    }

                    send(nsd, header, header_len, MSG_NOSIGNAL);

                    char file_buf[4096];
                    ssize_t n;
                    while ((n = read(fd, file_buf, sizeof(file_buf))) > 0) {
                        send(nsd, file_buf, n, MSG_NOSIGNAL);
                    }
                    close(fd);
                }
            }
        }

        close(nsd);
    }

    return 0;
}
