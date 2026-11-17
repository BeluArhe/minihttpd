#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <errno.h>
#include "http.h"

static int is_valid_token_chars(const char *start, size_t len) {
    if (len == 0) {
        return 0;
    }
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)start[i];
        if (c <= 0x20 || c == 0x7F) {
            return 0;
        }
    }
    return 1;
}

static const char *find_crlf(const char *buf, size_t len) {
    for (size_t i = 0; i + 1 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n') {
            return &buf[i];
        }
        if (buf[i] == '\n' || buf[i] == '\r') {
            return NULL; /* Salto de línea suelto sin par \r\n */
        }
    }
    return NULL;
}

int http_parse_request(const char *raw_buf, size_t len, http_request_t *req) {
    if (raw_buf == NULL || req == NULL || len == 0) {
        return 400;
    }

    memset(req, 0, sizeof(*req));

    /* 1. Localizar el primer "\r\n" (fin de la Request-Line) */
    const char *line_end = find_crlf(raw_buf, len);
    if (line_end == NULL) {
        return 400;
    }

    size_t line_len = (size_t)(line_end - raw_buf);

    const char *sp1 = memchr(raw_buf, ' ', line_len);
    if (sp1 == NULL) {
        return 400;
    }

    size_t rem_after_sp1 = (size_t)(line_end - (sp1 + 1));
    const char *sp2 = memchr(sp1 + 1, ' ', rem_after_sp1);
    if (sp2 == NULL) {
        return 400;
    }

    size_t rem_after_sp2 = (size_t)(line_end - (sp2 + 1));
    if (memchr(sp2 + 1, ' ', rem_after_sp2) != NULL) {
        return 400;
    }

    size_t method_len  = (size_t)(sp1 - raw_buf);
    size_t uri_len     = (size_t)(sp2 - (sp1 + 1));
    size_t version_len = rem_after_sp2;

    if (!is_valid_token_chars(raw_buf, method_len) ||
        !is_valid_token_chars(sp1 + 1, uri_len) ||
        !is_valid_token_chars(sp2 + 1, version_len)) {
        return 400;
    }

    if (method_len >= HTTP_MAX_METHOD ||
        uri_len >= HTTP_MAX_URI ||
        version_len >= HTTP_MAX_VERSION) {
        return 400;
    }

    memcpy(req->method, raw_buf, method_len);
    req->method[method_len] = '\0';

    memcpy(req->uri, sp1 + 1, uri_len);
    req->uri[uri_len] = '\0';

    memcpy(req->version, sp2 + 1, version_len);
    req->version[version_len] = '\0';

    if (req->uri[0] != '/') {
        return 400;
    }

    int is_http11 = 0;
    if (strcmp(req->version, "HTTP/1.1") == 0) {
        is_http11 = 1;
        req->keep_alive = 1; /* HTTP/1.1 persistente por defecto */
    } else if (strcmp(req->version, "HTTP/1.0") == 0) {
        is_http11 = 0;
        req->keep_alive = 0; /* HTTP/1.0 cerrado por defecto */
    } else {
        return 400;
    }

    /* 2. Parsear las líneas de encabezados hasta encontrar la línea vacía "\r\n" */
    const char *cur = line_end + 2;
    const char *buf_end = raw_buf + len;
    int found_end_of_headers = 0;
    int has_host = 0;
    int has_content_length = 0;
    int has_body_indicator = 0;

    while (cur < buf_end) {
        const char *hdr_end = find_crlf(cur, (size_t)(buf_end - cur));
        if (hdr_end == NULL) {
            return 400;
        }

        size_t hdr_len = (size_t)(hdr_end - cur);
        if (hdr_len == 0) {
            found_end_of_headers = 1;
            break;
        }

        const char *colon = memchr(cur, ':', hdr_len);
        if (colon == NULL || colon == cur) {
            return 400;
        }

        size_t name_len = (size_t)(colon - cur);
        if (!is_valid_token_chars(cur, name_len)) {
            return 400;
        }

        const char *val_start = colon + 1;
        while (val_start < hdr_end && (*val_start == ' ' || *val_start == '\t')) {
            val_start++;
        }
        const char *val_end = hdr_end;
        while (val_end > val_start && (val_end[-1] == ' ' || val_end[-1] == '\t')) {
            val_end--;
        }
        size_t val_len = (size_t)(val_end - val_start);

        if (name_len == 4 && strncasecmp(cur, "Host", 4) == 0) {
            if (has_host || val_len == 0 || val_len >= HTTP_MAX_HOST) {
                return 400;
            }
            memcpy(req->host, val_start, val_len);
            req->host[val_len] = '\0';
            has_host = 1;
        } else if (name_len == 10 && strncasecmp(cur, "User-Agent", 10) == 0) {
            size_t copy_len = (val_len < HTTP_MAX_USER_AGENT - 1) ? val_len : (HTTP_MAX_USER_AGENT - 1);
            memcpy(req->user_agent, val_start, copy_len);
            req->user_agent[copy_len] = '\0';
        } else if (name_len == 10 && strncasecmp(cur, "Connection", 10) == 0) {
            if (strncasecmp(val_start, "close", 5) == 0) {
                req->keep_alive = 0;
            } else if (strncasecmp(val_start, "keep-alive", 10) == 0) {
                req->keep_alive = 1;
            }
        } else if (name_len == 14 && strncasecmp(cur, "Content-Length", 14) == 0) {
            if (has_content_length) {
                return 400; /* Content-Length duplicado -> 400 Bad Request */
            }
            has_content_length = 1;

            if (val_len == 0) {
                return 400;
            }
            char cl_buf[32];
            if (val_len >= sizeof(cl_buf)) {
                return 400;
            }
            memcpy(cl_buf, val_start, val_len);
            cl_buf[val_len] = '\0';

            errno = 0;
            char *endptr = NULL;
            long cl_val = strtol(cl_buf, &endptr, 10);
            if (errno != 0 || *endptr != '\0' || endptr == cl_buf) {
                return 400;
            }
            if (cl_val < 0) {
                return 400;
            }
            if (cl_val > 0) {
                has_body_indicator = 1; /* Bloquear cuerpos > 0 bytes en GET */
            }
        } else if (name_len == 17 && strncasecmp(cur, "Transfer-Encoding", 17) == 0) {
            return 400; /* No soportamos chunked ni cuerpos en GET */
        }

        cur = hdr_end + 2;
    }

    if (!found_end_of_headers) {
        return 400;
    }

    /* RFC 9112: HTTP/1.1 exige cabecera Host obligatoria */
    if (is_http11 && !has_host) {
        return 400;
    }

    /* Validación de métodos (solo GET) y prevención de Request Smuggling */
    if (strcmp(req->method, "GET") != 0) {
        return 405;
    }
    if (has_body_indicator) {
        return 400;
    }

    return 0;
}

int http_format_error(char *buf, size_t buf_size, int status_code, const char *status_text) {
    if (!buf || !status_text || buf_size == 0) {
        return -1;
    }

    char body[256];
    int body_len = snprintf(body, sizeof(body), "<h1>%d %s</h1>", status_code, status_text);
    if (body_len < 0 || (size_t)body_len >= sizeof(body)) {
        return -1;
    }

    int resp_len = snprintf(buf, buf_size,
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        status_code, status_text, body_len, body);

    if (resp_len < 0 || (size_t)resp_len >= buf_size) {
        return -1;
    }
    return resp_len;
}

int http_format_headers(char *buf, size_t buf_size, const char *mime_type, off_t content_length, int keep_alive) {
    if (!buf || !mime_type || content_length < 0 || buf_size == 0) {
        return -1;
    }

    const char *conn_hdr = keep_alive ? "keep-alive" : "close";
    int header_len = snprintf(buf, buf_size,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Content-Length: %lld\r\n"
        "Connection: %s\r\n"
        "\r\n",
        mime_type, (long long)content_length, conn_hdr);

    if (header_len < 0 || (size_t)header_len >= buf_size) {
        return -1;
    }
    return header_len;
}
