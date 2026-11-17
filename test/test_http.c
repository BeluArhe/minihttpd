#include <stdio.h>
#include <string.h>
#include "http.h"

typedef struct {
    const char *name;
    const char *raw;
    int expected_status;
    const char *expected_method;
    const char *expected_uri;
    const char *expected_host;
    int expected_keep_alive;
} test_case_t;

int main(void) {
    test_case_t cases[] = {
        {
            "Caso 1: GET valido HTTP/1.1 con Host",
            "GET /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n",
            0, "GET", "/index.html", "localhost", 1
        },
        {
            "Caso 2: Regresion - URI con tabulador",
            "GET\t/index.html\tHTTP/1.1\r\nHost: localhost\r\n\r\n",
            400, "", "", "", 0
        },
        {
            "Caso 3: Regresion - Doble espacio en Request-Line",
            "GET  /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n",
            400, "", "", "", 0
        },
        {
            "Caso 4: Regresion - Salto de linea \n suelto sin \r",
            "GET /index.html HTTP/1.1\nHost: localhost\n\n",
            400, "", "", "", 0
        },
        {
            "Caso 5: Regresion - Texto sobrante despues de la version",
            "GET /index.html HTTP/1.1 extra\r\nHost: localhost\r\n\r\n",
            400, "", "", "", 0
        },
        {
            "Caso 6: Metodo no permitido (POST)",
            "POST /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n",
            405, "", "", "", 0
        },
        {
            "Caso 7: Version HTTP desconocida o invalida (HTTP/9.9)",
            "GET /index.html HTTP/9.9\r\nHost: localhost\r\n\r\n",
            400, "", "", "", 0
        },
        {
            "Caso 8: HTTP/1.0 sin Host obligatorio (Valido en 1.0)",
            "GET /index.html HTTP/1.0\r\n\r\n",
            0, "GET", "/index.html", "", 0
        },
        {
            "Caso 9: HTTP/1.0 con Connection: keep-alive explicito",
            "GET /index.html HTTP/1.0\r\nConnection: keep-alive\r\n\r\n",
            0, "GET", "/index.html", "", 1
        },
        {
            "Caso 10: HTTP/1.1 sin cabecera Host obligatoria",
            "GET /index.html HTTP/1.1\r\n\r\n",
            400, "", "", "", 0
        },
        {
            "Caso 11: Host duplicado",
            "GET / HTTP/1.1\r\nHost: a.com\r\nHost: b.com\r\n\r\n",
            400, "", "", "", 0
        },
        {
            "Caso 12: Host vacio",
            "GET / HTTP/1.1\r\nHost:   \r\n\r\n",
            400, "", "", "", 0
        },
        {
            "Caso 13: Cabecera con mayusculas mezcladas (hOsT)",
            "GET / HTTP/1.1\r\nhOsT: example.com\r\n\r\n",
            0, "GET", "/", "example.com", 1
        },
        {
            "Caso 14: Encabezado sin dos puntos (:)",
            "GET / HTTP/1.1\r\nHost localhost\r\n\r\n",
            400, "", "", "", 0
        },
        {
            "Caso 15: Espacio antes de los dos puntos (:)",
            "GET / HTTP/1.1\r\nHost : localhost\r\n\r\n",
            400, "", "", "", 0
        },
        {
            "Caso 16: GET con Content-Length: 0 (Valido)",
            "GET /index.html HTTP/1.1\r\nHost: localhost\r\nContent-Length: 0\r\n\r\n",
            0, "GET", "/index.html", "localhost", 1
        },
        {
            "Caso 17: GET con Content-Length: 10 (Smuggling prevention)",
            "GET /index.html HTTP/1.1\r\nHost: localhost\r\nContent-Length: 10\r\n\r\n",
            400, "", "", "", 0
        },
        {
            "Caso 18: Content-Length negativo (-1)",
            "GET /index.html HTTP/1.1\r\nHost: localhost\r\nContent-Length: -1\r\n\r\n",
            400, "", "", "", 0
        },
        {
            "Caso 19: Content-Length basura (abc)",
            "GET /index.html HTTP/1.1\r\nHost: localhost\r\nContent-Length: abc\r\n\r\n",
            400, "", "", "", 0
        },
        {
            "Caso 20: Content-Length con desbordamiento",
            "GET /index.html HTTP/1.1\r\nHost: localhost\r\nContent-Length: 99999999999999999999\r\n\r\n",
            400, "", "", "", 0
        },
        {
            "Caso 21: Content-Length duplicado",
            "GET / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 0\r\nContent-Length: 0\r\n\r\n",
            400, "", "", "", 0
        },
        {
            "Caso 22: TRANSFER-ENCODING en mayusculas (chunked)",
            "GET /index.html HTTP/1.1\r\nHost: localhost\r\nTRANSFER-ENCODING: chunked\r\n\r\n",
            400, "", "", "", 0
        },
        {
            "Caso 23: Connection: close explicito",
            "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
            0, "GET", "/", "localhost", 0
        }
    };

    int num_cases = sizeof(cases) / sizeof(cases[0]);
    int failed = 0;

    printf("=== EJECUTANDO TEST UNITARIO EXHAUSTIVO DEL PARSER HTTP ===\n");
    for (int i = 0; i < num_cases; i++) {
        http_request_t req;
        size_t len = strlen(cases[i].raw);
        int status = http_parse_request(cases[i].raw, len, &req);

        int ok = 1;
        if (status != cases[i].expected_status) {
            ok = 0;
        } else if (status == 0) {
            if (strcmp(req.method, cases[i].expected_method) != 0 ||
                strcmp(req.uri, cases[i].expected_uri) != 0 ||
                strcmp(req.host, cases[i].expected_host) != 0 ||
                req.keep_alive != cases[i].expected_keep_alive) {
                ok = 0;
            }
        }

        if (ok) {
            printf("[PASS] %s\n", cases[i].name);
        } else {
            printf("[FAIL] %s (Esperaba status %d/KA %d, obtuvo status %d/KA %d)\n",
                   cases[i].name, cases[i].expected_status, cases[i].expected_keep_alive,
                   status, req.keep_alive);
            failed++;
        }
    }

    printf("===========================================================\n");
    if (failed > 0) {
        printf("RESULTADO: %d pruebas fallaron.\n", failed);
        return 1;
    }
    printf("RESULTADO: Todas las pruebas pasaron exitosamente.\n");
    return 0;
}
