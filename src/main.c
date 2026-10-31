#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

int passiveTCP(const char *service, int qlen);
int errexit(const char *format, ...);

#define BUFSIZE 4096

int main(int argc, char *argv[]) {
    char *service = (argc > 1) ? argv[1] : "8080";

    struct sockaddr_in addrclt;
    socklen_t addrSZ;
    char fname[512], buf[BUFSIZE];
    int fd, sd, nsd;
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

            char method[16], url[256], protocol[16];
            sscanf(buf, "%s %s %s", method, url, protocol);

            if (strcmp(url, "/") == 0) {
                snprintf(fname, sizeof(fname), "www/index.html");
            } else {
                snprintf(fname, sizeof(fname), "www%s", url);
            }

            fd = open(fname, O_RDONLY);
            if (fd >= 0) {
                char *header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n";
                send(nsd, header, strlen(header), 0);

                char file_buf[1024];
                ssize_t n;
                while ((n = read(fd, file_buf, sizeof(file_buf))) > 0) {
                    send(nsd, file_buf, n, 0);
                }

                close(fd);
            } else {
                char *not_found = "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\n\r\n<h1>404 No Encontrado</h1>";
                send(nsd, not_found, strlen(not_found), 0);
            }
        }

        close(nsd);
    }

    return 0;
}
