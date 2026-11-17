#ifndef SERVER_H
#define SERVER_H

#include <stddef.h>
#include <time.h>
#include <sys/types.h>

#define CONN_BUF_SIZE    4096
#define CONN_TIMEOUT_SEC 10

typedef enum {
    CONN_READING_REQ = 0,
    CONN_SENDING_HEADERS,
    CONN_SENDING_FILE
} conn_state_t;

typedef struct client_conn {
    int fd;
    conn_state_t state;

    char read_buf[CONN_BUF_SIZE];
    size_t read_len;

    char write_buf[512];
    size_t write_len;
    size_t write_offset;

    int file_fd;
    off_t file_offset;
    off_t file_size;

    int keep_alive;
    time_t last_active;

    struct client_conn *prev;
    struct client_conn *next;
} client_conn_t;

void server_run(int listen_sd, const char *root_dir);

#endif /* SERVER_H */
