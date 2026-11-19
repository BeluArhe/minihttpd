# MiniHTTPd

Servidor HTTP/1.1 concurrente y dirigido por eventos (*event-driven*) implementado en C sobre Linux mediante `epoll` y sockets TCP no bloqueantes, diseñado para servir contenido estático de forma segura y eficiente en un único hilo de ejecución.

---

## Características principales

* **Concurrencia event-driven (`epoll`):** Multiplexación de E/S en un solo hilo usando `epoll_create1`, `epoll_ctl` y `epoll_wait`, con sockets no bloqueantes y manejo de estados por conexión (`EPOLLIN` / `EPOLLOUT`).
* **Soporte HTTP/1.0 y HTTP/1.1:**
  * Acumulación incremental de cabeceras hasta `\r\n\r\n` y validación estricta de la línea de petición y encabezados.
  * Manejo de conexiones persistentes (`keep-alive` por defecto en HTTP/1.1 y explícito en HTTP/1.0) y cierre por inactividad (*timeout* de 10 segundos).
  * Soporte exclusivo para el método `GET` (responde `405 Method Not Allowed` a métodos como `POST` o `HEAD`).
* **Seguridad y endurecimiento del protocolo:**
  * **Prevención de request smuggling:** Rechazo con `400 Bad Request` ante peticiones `GET` con cuerpo (`Content-Length > 0`), valores negativos, malformados, duplicados, desbordamientos o `Transfer-Encoding`.
  * **Control de rutas y path enumeration:** Resolución canónica mediante `realpath()` y verificación de prefijo contra el directorio raíz (`www/`), devolviendo `404 Not Found` para rutas fuera del directorio permitido y `403 Forbidden` ante restricciones de permisos internas.
  * **Lectura segura de archivos:** Uso de `fstat()` sobre el descriptor abierto para obtener metadatos y `pread()` con desplazamiento explícito para envíos parciales sin bloqueo.
  * **Tipos MIME y nosniff:** Asignación de `Content-Type` según extensión (con respaldo `application/octet-stream`) y cabecera `X-Content-Type-Options: nosniff`.
  * **Gestión defensiva de memoria:** Control estricto de límites de búfer (máximo 4 KB en cabeceras) y uso de funciones acotadas en lugar de `strcpy` o `sprintf`.

---

## Estructura del proyecto

```text
minihttpd/
├── src/            # Código fuente del servidor (bucle epoll, parser HTTP, archivos, MIME)
├── include/        # Archivos de cabecera (.h)
├── test/           # Pruebas unitarias del parser HTTP (test_http.c)
├── www/            # Directorio raíz público servido por HTTP
└── Makefile        # Reglas de compilación y ejecución
