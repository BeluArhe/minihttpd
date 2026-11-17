#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "server.h"
#include "http.h"
#include "files.h"
#include "mime.h"

#define MAX_EVENTS      64
#define FILE_CHUNK_SIZE 4096
#define EPOLL_TICK_MS   1000

static client_conn_t *conn_list_head = NULL;

static void conn_list_add(client_conn_t *conn) {
    conn->prev = NULL;
    conn->next = conn_list_head;
    if (conn_list_head) {
        conn_list_head->prev = conn;
    }
    conn_list_head = conn;
}

static void conn_list_remove(client_conn_t *conn) {
    if (conn->prev) {
        conn->prev->next = conn->next;
    } else {
        conn_list_head = conn->next;
    }
    if (conn->next) {
        conn->next->prev = conn->prev;
    }
    conn->prev = NULL;
    conn->next = NULL;
}

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void close_connection(int epfd, client_conn_t *conn) {
    if (!conn) {
        return;
    }
    conn_list_remove(conn);
    epoll_ctl(epfd, EPOLL_CTL_DEL, conn->fd, NULL);
    close(conn->fd);
    if (conn->file_fd >= 0) {
        close(conn->file_fd);
        conn->file_fd = -1;
    }
    free(conn);
}

static void prepare_error_response(int epfd, client_conn_t *conn, int code, const char *msg) {
    if (conn->file_fd >= 0) {
        close(conn->file_fd);
        conn->file_fd = -1;
    }
    conn->keep_alive = 0;
    conn->read_len = 0;

    int len = http_format_error(conn->write_buf, sizeof(conn->write_buf), code, msg);
    if (len < 0) {
        close_connection(epfd, conn);
        return;
    }
    conn->write_len = (size_t)len;
    conn->write_offset = 0;
    conn->state = CONN_SENDING_HEADERS;

    struct epoll_event ev;
    ev.events = EPOLLOUT;
    ev.data.ptr = conn;
    if (epoll_ctl(epfd, EPOLL_CTL_MOD, conn->fd, &ev) < 0) {
        close_connection(epfd, conn);
    }
}

static ssize_t find_header_end(const char *buf, size_t len) {
    for (size_t i = 0; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' &&
            buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            return (ssize_t)(i + 4);
        }
    }
    return -1;
}

static void process_buffered_request(int epfd, client_conn_t *conn, const char *root_dir, size_t hdr_end) {
    http_request_t req;
    int parse_status = http_parse_request(conn->read_buf, hdr_end, &req);

    size_t remaining = conn->read_len - hdr_end;
    if (remaining > 0) {
        memmove(conn->read_buf, conn->read_buf + hdr_end, remaining);
    }
    conn->read_len = remaining;
    conn->read_buf[conn->read_len] = '\0';

    if (parse_status != 0) {
        if (parse_status == 405) {
            prepare_error_response(epfd, conn, 405, "Method Not Allowed");
        } else {
            prepare_error_response(epfd, conn, 400, "Bad Request");
        }
        return;
    }

    conn->keep_alive = req.keep_alive;

    struct stat st;
    char resolved_path[PATH_MAX];
    file_status_t fstatus = file_open_safe(root_dir, req.uri, &conn->file_fd, &st,
                                           resolved_path, sizeof(resolved_path));
    if (fstatus != FILE_OK) {
        if (fstatus == FILE_ERR_FORBIDDEN) {
            prepare_error_response(epfd, conn, 403, "Forbidden");
        } else if (fstatus == FILE_ERR_NOT_FOUND) {
            prepare_error_response(epfd, conn, 404, "Not Found");
        } else {
            prepare_error_response(epfd, conn, 500, "Internal Server Error");
        }
        return;
    }

    conn->file_size = st.st_size;
    conn->file_offset = 0;

    const char *mime_type = get_mime_type(resolved_path);
    int hlen = http_format_headers(conn->write_buf, sizeof(conn->write_buf),
                                   mime_type, conn->file_size, conn->keep_alive);
    if (hlen < 0) {
        prepare_error_response(epfd, conn, 500, "Internal Server Error");
        return;
    }

    conn->write_len = (size_t)hlen;
    conn->write_offset = 0;
    conn->state = CONN_SENDING_HEADERS;

    struct epoll_event ev;
    ev.events = EPOLLOUT;
    ev.data.ptr = conn;
    if (epoll_ctl(epfd, EPOLL_CTL_MOD, conn->fd, &ev) < 0) {
        close_connection(epfd, conn);
    }
}

static void handle_read_state(int epfd, client_conn_t *conn, const char *root_dir) {
    while (conn->read_len < sizeof(conn->read_buf) - 1) {
        ssize_t n = read(conn->fd,
                         conn->read_buf + conn->read_len,
                         sizeof(conn->read_buf) - 1 - conn->read_len);
        if (n > 0) {
            conn->read_len += (size_t)n;
            conn->read_buf[conn->read_len] = '\0';
            conn->last_active = time(NULL);
            if (find_header_end(conn->read_buf, conn->read_len) >= 0) {
                break;
            }
        } else if (n == 0) {
            if (find_header_end(conn->read_buf, conn->read_len) >= 0) {
                break;
            }
            close_connection(epfd, conn);
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            if (errno == EINTR) {
                continue;
            }
            close_connection(epfd, conn);
            return;
        }
    }

    ssize_t hdr_end = find_header_end(conn->read_buf, conn->read_len);
    if (hdr_end < 0) {
        prepare_error_response(epfd, conn, 400, "Bad Request");
        return;
    }

    process_buffered_request(epfd, conn, root_dir, (size_t)hdr_end);
}

static void finish_or_keep_alive(int epfd, client_conn_t *conn, const char *root_dir) {
    if (conn->file_fd >= 0) {
        close(conn->file_fd);
        conn->file_fd = -1;
    }

    if (!conn->keep_alive) {
        close_connection(epfd, conn);
        return;
    }

    conn->state = CONN_READING_REQ;
    conn->write_len = 0;
    conn->write_offset = 0;
    conn->file_offset = 0;
    conn->file_size = 0;
    conn->last_active = time(NULL);

    ssize_t next_hdr_end = find_header_end(conn->read_buf, conn->read_len);
    if (next_hdr_end >= 0) {
        process_buffered_request(epfd, conn, root_dir, (size_t)next_hdr_end);
        return;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.ptr = conn;
    if (epoll_ctl(epfd, EPOLL_CTL_MOD, conn->fd, &ev) < 0) {
        close_connection(epfd, conn);
    }
}

static void handle_write_state(int epfd, client_conn_t *conn, const char *root_dir) {
    if (conn->state == CONN_SENDING_HEADERS) {
        while (conn->write_offset < conn->write_len) {
            ssize_t sent = send(conn->fd,
                                conn->write_buf + conn->write_offset,
                                conn->write_len - conn->write_offset,
                                MSG_NOSIGNAL);
            if (sent > 0) {
                conn->write_offset += (size_t)sent;
                conn->last_active = time(NULL);
            } else if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                return;
            } else if (sent < 0 && errno == EINTR) {
                continue;
            } else {
                close_connection(epfd, conn);
                return;
            }
        }

        if (conn->file_fd < 0) {
            close_connection(epfd, conn);
            return;
        }
        conn->state = CONN_SENDING_FILE;
    }

    if (conn->state == CONN_SENDING_FILE) {
        char chunk[FILE_CHUNK_SIZE];
        while (conn->file_offset < conn->file_size) {
            ssize_t nread = pread(conn->file_fd, chunk, sizeof(chunk), conn->file_offset);
            if (nread <= 0) {
                close_connection(epfd, conn);
                return;
            }

            ssize_t sent = send(conn->fd, chunk, (size_t)nread, MSG_NOSIGNAL);
            if (sent > 0) {
                conn->file_offset += sent;
                conn->last_active = time(NULL);
            } else if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                return;
            } else if (sent < 0 && errno == EINTR) {
                continue;
            } else {
                close_connection(epfd, conn);
                return;
            }
        }

        finish_or_keep_alive(epfd, conn, root_dir);
    }
}

static void sweep_inactive_connections(int epfd) {
    time_t now = time(NULL);
    client_conn_t *cur = conn_list_head;
    while (cur != NULL) {
        client_conn_t *next = cur->next;
        if (now - cur->last_active >= CONN_TIMEOUT_SEC) {
            close_connection(epfd, cur);
        }
        cur = next;
    }
}

void server_run(int listen_sd, const char *root_dir) {
    if (set_nonblocking(listen_sd) < 0) {
        perror("set_nonblocking(listen_sd)");
        return;
    }

    int epfd = epoll_create1(0);
    if (epfd < 0) {
        perror("epoll_create1");
        return;
    }

    int reserve_fd = open("/dev/null", O_RDONLY);

    struct epoll_event ev, events[MAX_EVENTS];
    ev.events = EPOLLIN;
    ev.data.ptr = NULL;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, listen_sd, &ev) < 0) {
        perror("epoll_ctl(listen_sd)");
        if (reserve_fd >= 0) close(reserve_fd);
        close(epfd);
        return;
    }

    while (1) {
        int nfds = epoll_wait(epfd, events, MAX_EVENTS, EPOLL_TICK_MS);
        if (nfds < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < nfds; i++) {
            if (events[i].data.ptr == NULL) {
                while (1) {
                    struct sockaddr_in addrclt;
                    socklen_t addrSZ = sizeof(addrclt);
                    int nsd = accept(listen_sd, (struct sockaddr *)&addrclt, &addrSZ);
                    if (nsd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break;
                        }
                        if (errno == EINTR) {
                            continue;
                        }
                        if ((errno == EMFILE || errno == ENFILE) && reserve_fd >= 0) {
                            close(reserve_fd);
                            nsd = accept(listen_sd, NULL, NULL);
                            if (nsd >= 0) {
                                close(nsd);
                            }
                            reserve_fd = open("/dev/null", O_RDONLY);
                            break;
                        }
                        perror("accept");
                        break;
                    }

                    if (set_nonblocking(nsd) < 0) {
                        close(nsd);
                        continue;
                    }

                    client_conn_t *conn = calloc(1, sizeof(client_conn_t));
                    if (!conn) {
                        close(nsd);
                        continue;
                    }
                    conn->fd = nsd;
                    conn->file_fd = -1;
                    conn->state = CONN_READING_REQ;
                    conn->last_active = time(NULL); /* Inicialización correcta */

                    struct epoll_event cev;
                    cev.events = EPOLLIN;
                    cev.data.ptr = conn;
                    if (epoll_ctl(epfd, EPOLL_CTL_ADD, nsd, &cev) < 0) {
                        close(nsd);
                        free(conn);
                        continue;
                    }
                    conn_list_add(conn);
                }
            } else {
                client_conn_t *conn = (client_conn_t *)events[i].data.ptr;
                uint32_t revents = events[i].events;

                if (revents & (EPOLLERR | EPOLLHUP)) {
                    close_connection(epfd, conn);
                    continue;
                }

                if ((revents & EPOLLIN) && conn->state == CONN_READING_REQ) {
                    handle_read_state(epfd, conn, root_dir);
                } else if ((revents & EPOLLOUT) &&
                           (conn->state == CONN_SENDING_HEADERS || conn->state == CONN_SENDING_FILE)) {
                    handle_write_state(epfd, conn, root_dir);
                }
            }
        }

        /* Barrido de inactividad ejecutado FUERA del bucle de eventos (evita use-after-free) */
        sweep_inactive_connections(epfd);
    }

    if (reserve_fd >= 0) {
        close(reserve_fd);
    }
    close(epfd);
}
