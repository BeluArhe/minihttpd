/* passivesock.c - allocate & bind a server socket using getaddrinfo */

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

extern int errno;
int errexit(const char *format, ...);

/*------------------------------------------------------------------------
 * passivesock - allocate & bind a server socket using TCP or UDP
 *------------------------------------------------------------------------
 */
int passivesock(const char *service, const char *transport, int qlen)
{
    struct addrinfo hints, *res, *p;
    int s= -1, status, type;

    // 1. Determinar el tipo de socket según el transporte solicitado [cite: 17]
    if (strcmp(transport, "udp") == 0)
        type = SOCK_DGRAM;
    else
        type = SOCK_STREAM;

    // 2. Configurar las "pistas" (hints) para el filtro de getaddrinfo [cite: 193]
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;       // IPv4 (o AF_UNSPEC para IPv4/IPv6 dual) [cite: 193]
    hints.ai_socktype = type;          // SOCK_STREAM o SOCK_DGRAM [cite: 193]
    hints.ai_flags    = AI_PASSIVE;    // Servidor: node=NULL equivale a INADDR_ANY [cite: 193, 194]

    // 3. Resolver servicio/puerto y obtener las estructuras de red directamente [cite: 192, 194]
    status = getaddrinfo(NULL, service, &hints, &res);
    if (status != 0)
        errexit("getaddrinfo failed for \"%s\": %s\n", service, gai_strerror(status));

    // 4. Iterar sobre la lista de resultados e intentar crear y enlazar el socket [cite: 193]
    for (p = res; p != NULL; p = p->ai_next) {
        s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s < 0)
            continue;

        if (bind(s, p->ai_addr, p->ai_addrlen) == 0)
            break; // Éxito en bind [cite: 193]

        close(s); // Si bind falla, cerrar descriptor y probar siguiente [cite: 193]
    }

    freeaddrinfo(res); // Liberar memoria asignada dinámicamente por getaddrinfo [cite: 193]

    if (p == NULL)
        errexit("can't bind to \"%s\" port: %s\n", service, strerror(errno));

    // 5. Si es TCP, colocar el socket en modo pasivo de escucha (listen) [cite: 17, 193]
    if (type == SOCK_STREAM && listen(s, qlen) < 0)
        errexit("can't listen on \"%s\" port: %s\n", service, strerror(errno));

    return s;
}
