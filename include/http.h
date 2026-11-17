#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>
#include <sys/types.h>

#define HTTP_MAX_METHOD     16
#define HTTP_MAX_URI        256
#define HTTP_MAX_VERSION    16
#define HTTP_MAX_HOST       128
#define HTTP_MAX_USER_AGENT 256

typedef struct {
    char method[HTTP_MAX_METHOD];
    char uri[HTTP_MAX_URI];
    char version[HTTP_MAX_VERSION];
    char host[HTTP_MAX_HOST];
    char user_agent[HTTP_MAX_USER_AGENT];
    int  keep_alive;
} http_request_t;

int http_parse_request(const char *raw_buf, size_t len, http_request_t *req);
int http_format_error(char *buf, size_t buf_size, int status_code, const char *status_text);
int http_format_headers(char *buf, size_t buf_size, const char *mime_type, off_t content_length, int keep_alive);

#endif /* HTTP_H */
