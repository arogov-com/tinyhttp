// Copyright (C) 2024 Aleksei Rogov <alekzzzr@gmail.com>. All rights reserved.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/limits.h>
#include <bits/local_lim.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/epoll.h>
#include <sys/wait.h>
#include "tinyhttp.h"
#include "map.h"


uint8_t verbose = 0;
char root[PATH_MAX] = {0};
char host[HOST_NAME_MAX] = {0};
struct MAP config = {.objects = NULL, .length = 0};

char *cgi_str(char *str, int n) {
    if(str == NULL) {
        return NULL;
    }
    if(n == 0) {
        return str;
    }
    while(n--) {
        if(str[n] >= 'a' && str[n] <= 'z') {
            str[n] = str[n] - 0x20;
        }
        else if(str[n] == '-') {
            str[n] = '_';
        }
    }
    return str;
}

char **cgi_env(struct MAP *map, request_t *req, int sock) {
    char **env = malloc((map->count + 1 + PREDEF_ENV) * sizeof(void*));

    char *query = malloc(4096);
    char *script = malloc(4096);
    char *uri = malloc(4096);
    char *doc = malloc(4096);
    char *doc_root = malloc(4096);
    char *script_file = malloc(4096);
    char *raddr = malloc(28);
    char *rport = malloc(18);
    char *saddr = malloc(28);
    char *sport = malloc(18);
    char *server_name = malloc(HOST_NAME_MAX + 14);

    env[0] = FCGI_ROLE;
    if(req->method == GET) {
        env[1] = REQUEST_METHOD_GET;
    }
    else if(req->method == POST) {
        env[1] = REQUEST_METHOD_POST;
    }
    env[2] = GATEWAY_INTERFACE;
    env[3] = SERVER_SOFTWARE;
    env[4] = REQUEST_SCHEME;
    env[5] = SERVER_PROTOCOL;

    snprintf(query, 4096, QUERY_STRING, req->query ? req->query : "");
    env[6] = query;

    snprintf(script, 4096, SCRIPT_NAME, req->path ? req->path : "");
    env[7] = script;

    snprintf(uri, 4096, REQUEST_URI, req->path ? req->path : "", req->query ? req->query : "");
    env[8] = uri;

    snprintf(doc, 4096, DOCUMENT_URI, req->path ? req->path : "");
    env[9] = doc;

    snprintf(script_file, 4096, SCRIPT_FILENAME, root, req->path);
    env[10] = script_file;

    snprintf(doc_root, 4096, DOCUMENT_ROOT, root);
    env[11] = doc_root;

    char str[INET_ADDRSTRLEN];
    struct sockaddr address;
    socklen_t address_len = 16;
    if(getsockname(sock, &address, &address_len) != 0) {
        return NULL;
    }
    inet_ntop(AF_INET, &((struct sockaddr_in*)&address)->sin_addr, str, INET_ADDRSTRLEN);
    snprintf(saddr, 28, SERVER_ADDR, str);
    snprintf(sport, 18, SERVER_PORT, htons(((struct sockaddr_in*)&address)->sin_port));

    if(getpeername(sock, &address, &address_len) != 0) {
        return NULL;
    }
    inet_ntop(AF_INET, &((struct sockaddr_in*)&address)->sin_addr, str, INET_ADDRSTRLEN);
    snprintf(raddr, 28, REMOTE_ADDR, str);
    snprintf(rport, 18, REMOTE_PORT, htons(((struct sockaddr_in*)&address)->sin_port));

    env[12] = saddr;
    env[13] = sport;
    env[14] = raddr;
    env[15] = rport;

    snprintf(server_name, HOST_NAME_MAX + 14, SERVER_HOST, host);
    env[16] = server_name;

    map_get_objects_start(map);
    for(int i = 0; i != map->count; ++i) {
        struct MAP_OBJECT *obj = map_get_objects_next(map);

        env[i + PREDEF_ENV] = malloc(obj->key_size + obj->value_size + 2 + 5);
        strcpy(env[i + PREDEF_ENV], "HTTP_");
        memcpy(env[i + PREDEF_ENV] + 5, obj->key, obj->key_size);
        cgi_str(env[i + PREDEF_ENV] + 5, obj->key_size);
        env[i + PREDEF_ENV][5 + obj->key_size] = '=';

        memcpy(env[i + PREDEF_ENV] + obj->key_size + 1 + 5, obj->value, obj->value_size);
        env[i + PREDEF_ENV][5 + obj->key_size + 2 + obj->value_size] = 0;
    }
    env[map->count + 1 + PREDEF_ENV] = NULL;

    return env;
}

int cgi_run(const char *command, int timeout, char *out_buffer, int out_buffer_size, int sock, struct MAP *map, request_t *req) {
    int pipefd[2];
    if(pipe(pipefd) < 0) {
        return CGI_PIPE_ERROR;
    }

    pid_t pid = fork();
    if(pid == -1) {
        return CGI_FORK_ERROR;
    }

    if(pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);

        char **p = cgi_env(map, req, sock);

        if(execle(command, command, NULL, p) == -1) {
            return CGI_EXEC_ERROR;
        }
    }

    int child_done = 0;
    while(timeout--) {
        int res = waitpid(pid, NULL, WNOHANG);
        if(res != 0) {
            child_done = 1;
            break;
        }
        usleep(100000);
    }

    if(timeout <= 0) {
        kill(pid, 9);
    }
    else if(child_done) {
        int status;
        wait(&status);
    }

    close(pipefd[1]);
    int rd = read(pipefd[0], out_buffer, out_buffer_size);
    close(pipefd[0]);

    return rd;
}

int response(int code, int sock, char *data, unsigned int data_len, char *content_type) {
    if(sock <= 0) {
        return -1;
    }

    if(data == NULL || data_len == 0) {
        return -2;
    }

    char *send_buff = malloc(SEND_BUFFER_SIZE);
    if(send_buff == NULL) {
        return -3;
    }

    int r = snprintf(send_buff, SEND_BUFFER_SIZE, "HTTP/1.1 %s\r\n\
Server: %s\r\n\
Content-Length: %i\r\n\
Content-Type: %s\r\n\
Connection: keep-alive\r\n\r\n", responses[code].msg, SERVER_NAME, data_len, content_type);

    memcpy(send_buff + r, data, data_len);
    send(sock, send_buff, r + data_len, 0);

    free(send_buff);
    return data_len;
}

void http_get(request_t *req, struct MAP *map, int sock, char *data, size_t data_len) {
    if(verbose) {
        printf("\"GET %s %s\" ", req->path, req->version);
    }

    char *file_path = malloc(PATH_MAX);
    if(file_path == NULL) {
        int rsz = response(RESPONSE_500, sock, responses[RESPONSE_500].msg, responses[RESPONSE_500].msg_len, "text/html");
        if(verbose) {
            printf("500 %i ", rsz);
        }
        return;
    }

    char *pt = stpcpy(file_path, root);
    struct CONFIG_PATH config_path;
    if(map_get(&config, req->path, strlen(req->path), &config_path, sizeof(struct CONFIG_PATH)) > 0) {
        if(config_path.action[0] != '$') {
            strcpy(pt, config_path.action);
        }
    }
    else if(map_get(&config, req->path, strrchr(req->path, '/') - req->path + 1, &config_path, sizeof(struct CONFIG_PATH)) > 0) {
            strcpy(pt, req->path + 1);
    }
    else {
        int rsz = response(RESPONSE_403, sock, responses[RESPONSE_403].msg, responses[RESPONSE_403].msg_len, "text/html");
        if(verbose) {
            printf("403 %i ", rsz);
        }
        free(file_path);
        return;
    }

    if(strcmp(config_path.action, "fastcgi") != 0 ) {
        int file = open(file_path, O_RDONLY);
        if(file < 0) {
            int rsz = response(RESPONSE_404, sock, responses[RESPONSE_404].msg, responses[RESPONSE_404].msg_len, "text/html");
            if(verbose) {
                printf("404 %i ", rsz);
            }
            free(file_path);
            return;
        }

        char *file_buff = malloc(FILE_BUFFER_SIZE);
        if(file_buff == NULL) {
            int rsz = response(RESPONSE_500, sock, responses[RESPONSE_500].msg, responses[RESPONSE_500].msg_len, "text/html");
            if(verbose) {
                printf("500 %i ", rsz);
            }
            close(file);
            free(file_path);
            return;
        }

        int rd = read(file, file_buff, FILE_BUFFER_SIZE);
        if(rd > 0) {
            int rsz = response(RESPONSE_200, sock, file_buff, rd, config_path.content_type);
            if(verbose) {
                printf("200 %i ", rsz);
            }
        }
        else {
            int rsz = response(RESPONSE_500, sock, responses[RESPONSE_500].msg, responses[RESPONSE_500].msg_len, "text/html");
            if(verbose) {
                printf("500 %i ", rsz);
            }
        }

        close(file);
        free(file_buff);
    }
    else {
        char *cgi_buff = malloc(CGI_BUFFER_SIZE);
        if(cgi_buff == NULL) {
            int rsz = response(RESPONSE_500, sock, responses[RESPONSE_500].msg, responses[RESPONSE_500].msg_len, "text/html");
            if(verbose) {
                printf("500 %i ", rsz);
            }
            free(cgi_buff);
            free(file_path);
            return;
        }

        int rd = cgi_run(file_path, 200, cgi_buff, CGI_BUFFER_SIZE, sock, map, req);
        if(rd <= 0) {
            int rsz = response(RESPONSE_502, sock, responses[RESPONSE_502].msg, responses[RESPONSE_502].msg_len, "text/html");
            if(verbose) {
                printf("502 %i ", rsz);
            }
            free(cgi_buff);
            free(file_path);
            return;
        }

        int rsz = response(RESPONSE_200, sock, cgi_buff, rd, config_path.content_type);
        free(cgi_buff);

        if(verbose) {
            printf("200 %i ", rsz);
        }
    }

    free(file_path);
}

void http_post(request_t *req, struct MAP *map, int sock, char *data, size_t data_len) {

}

int http_request(char *data, int data_length, int sock) {
    int length = data_length;
    if(data == NULL || data_length == 0) {
        if(verbose) {
            printf("empty request ");
        }
        return REQUEST_EMPTY;
    }
    if(length < sizeof(http_methods[0].name)) {
        if(verbose) {
            printf("invalid request ");
        }
        return REQUEST_INVALID;
    }

    request_t req;

    int error = REQUEST_METHOD_UNSUPPORTED;
    for(int i = 0; i != sizeof(http_methods) / sizeof(http_method_t); ++i) {
        if(memcmp(data, http_methods[i].name, http_methods[i].len) == 0) {
            error = 0;
            req.method = http_methods[i].key;
            data += http_methods[i].len;
            length -= http_methods[i].len;
            break;
        }
    }
    if(error < 0) {
        if(verbose) {
            printf("unsupported method ");
        }
        return error;
    }

    char *tmp = memchr(data, ' ', length);
    if(tmp == NULL || tmp == data || *data != '/') {
        if(verbose) {
            printf("invalid path ");
        }
        return REQUEST_INVALID_PATH;
    }
    req.path = data;
    *tmp = 0;
    length -= tmp - data + 1;
    data = tmp + 1;

    req.query = memchr(req.path, '?', tmp - req.path);
    if(req.query) {
        *req.query = 0;
        ++req.query;
    }

    if(length == 0) {
        if(verbose) {
            printf("protocol unsupported ");
        }
        return REQUEST_PROTOCOL_UNSUPPORTED;
    }

    tmp = memchr(data, '\r', length);
    if(tmp == NULL) {
        if(verbose) {
            printf("protocol unsupported ");
        }
        return REQUEST_PROTOCOL_UNSUPPORTED;
    }
    req.version = data;
    *tmp = 0;
    length -= tmp - data + 2;
    data = tmp + 2;

    if(*(uint64_t *)req.version != HTTP11_SIGNATURE) {
        if(verbose) {
            printf("protocol %s unsupported ", req.version);
        }
        return REQUEST_PROTOCOL_UNSUPPORTED;
    }

    if(length < 2) {
        if(verbose) {
            printf("invalid request ");
        }
        return REQUEST_INVALID;
    }

    struct MAP map = {.objects = NULL, .length = 0};

    while(length > 2) {
        char *key;
        int key_len;
        char *val;

        val = memchr(data, ':', length);
        if(val == NULL) {
            if(verbose) {
                printf("invalid headers ");
            }
            return REQUEST_INVALID_HEADERS;
        }
        key = data;
        key_len = val - data;
        length -= key_len + 1;
        data += key_len + 1;

        if(data[0] != ' ') {
            if(verbose) {
                printf("invalid headers ");
            }
            return REQUEST_INVALID_HEADERS;
        }
        ++data;
        --length;

        tmp = memchr(data, '\r', length);
        if(tmp) {
            map_add(&map, key, key_len, data, tmp - data);
            length -= tmp - data + 2;
            data += tmp - data + 2;
        }
        else {
            map_add(&map, key, key_len, data, length);
            break;
        }
    }

    switch(req.method) {
        case GET:
            http_get(&req, &map, sock, data, length);
            break;
        case POST:
            http_post(&req, &map, sock, data, length);
            break;
        default:
            int rsz = response(RESPONSE_405, sock, responses[RESPONSE_405].msg, responses[RESPONSE_405].msg_len, "text/html");
            if(verbose) {
                printf("405 %i ", rsz);
            }
            return REQUEST_METHOD_UNSUPPORTED;
    }

    if(verbose) {
        char user_agent[256];
        int user_agent_len = map_get(&map, "User-Agent", 10, user_agent, sizeof(user_agent));
        user_agent[user_agent_len] = 0;
        printf("\"%s\"", user_agent);
    }

    char connection[64];
    int qr = map_get(&map, "Connection", 10, connection, sizeof(connection));
    if(qr > 0) {
        connection[qr] = 0;
        if(memcmp(connection, "close", 6) == 0 || memcmp(connection, "Close", 6) == 0) {
            close(sock);
        }
    }

    map_destroy(&map);
    return 0;
}

int get_config(char *path) {
    FILE *f = fopen(path, "r");
    if(f == NULL) {
        return CONFIG_NOTFOUND;
    }

    char spath[128], stype[128], sact[128];
    char buff[512];

    while(fgets(buff, 512, f)) {
        if(buff[0] == '#') {
            continue;
        }

        int r = sscanf(buff, "%s %s %s\n", spath, stype, sact);
        if(r != 3) {
            continue;
        }

        struct CONFIG_PATH config_path;
        config_path.content_type = malloc(strlen(stype) + 1);
        if(config_path.content_type == NULL) {
            return CONFIG_MALLOC_ERROR;
        }
        strcpy(config_path.content_type, stype);

        config_path.action = malloc(strlen(sact) + 1);
        if(config_path.action == NULL) {
            free(config_path.content_type);
            return CONFIG_MALLOC_ERROR;
        }
        strcpy(config_path.action, sact);
        map_add(&config, spath, strlen(spath), &config_path, sizeof(struct CONFIG_PATH));
    }

    fclose(f);
    return 1;
}

void usage(char *argv0) {
    printf("%s server\n", SERVER_NAME);
    printf("Usage: %s [-v] [-w num] [-p port]\n", argv0);
    printf("  -v        : verbose\n");
    printf("  -w num    : workers number\n");
    printf("  -p port   : port\n");
    printf("  -r path   : root path\n");
    printf("  -c config : config path\n");
    printf("  -h        : print thist help\n");
}

int main(int argc, char *argv[]) {
    int workers = 0;
    uint16_t port = 0;
    char config_path[PATH_MAX] = "tinyhttp.conf";
    char str[INET_ADDRSTRLEN];

    extern char *optarg;
    int opt;
    while((opt = getopt(argc, argv, "vw:p:r:c:n:h")) > 0) {
        switch(opt) {
            case 'v':
                verbose = 1;
                break;
            case 'w':
                workers = atoi(optarg);
                break;
            case 'p':
                port = atoi(optarg);
                break;
            case 'r':
                strncpy(root, optarg, sizeof(root));
                int tmp = open(root, O_DIRECTORY);
                if(tmp < 0) {
                    printf("%s is not a directory\n", root);
                    return 1;
                }
                close(tmp);
                break;
            case 'c':
                strcpy(config_path, optarg);
                break;
            case 'n':
                strcpy(host, optarg);
                break;
            case 'h':
                usage(argv[0]);
                break;
            default:
                usage(argv[0]);
                return 1;
        }
    }
    if(workers-- == 0) {
        workers = WORKERS - 1;
    }
    if(port == 0) {
        port = DEFAULT_PORT;
    }
    if(root[0] == '\x0' && getcwd(root, PATH_MAX) == NULL) {
        printf("Can't get current directory\n");
        return -1;
    }
    int tmp = strlen(root) - 1;
    if(root[tmp] != '/') {
        root[tmp + 1] = '/';
        root[tmp + 2] = '\x0';
    }
    if(host[0] == '\x0' && gethostname(host, sizeof(host)) == -1) {
        printf("Can't get hostname\n");
        return 1;
    }

    int cr = get_config(config_path);
    if(cr < 0) {
        printf("Can't read config file %s (exit code: %i)\n", config_path, cr);
        return 1;
    }

    char *buffer = malloc(RECV_BUFFER_SIZE);
    if(buffer == NULL) {
        printf("malloc() error");
        return 1;
    }

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(sock == -1) {
        free(buffer);
        perror("socket() error");
        return 1;
    }

    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    server_address.sin_addr.s_addr = INADDR_ANY;

    if(bind(sock, (struct sockaddr*) &server_address, sizeof(server_address)) == -1) {
        perror("bind() error");
        close(sock);
        free(buffer);
        return 1;
    }

    if(listen(sock, MAX_CLIENTS) != 0) {
        perror("listen() error");
        close(sock);
        free(buffer);
        return 1;
    }

    struct epoll_event ev, events[MAX_EVENTS];
    int nfds, epollfd;
    epollfd = epoll_create1(0);
    if(epollfd == -1) {
        perror("epoll_create1() error");
        close(epollfd);
        close(sock);
        free(buffer);
        return 1;
    }

    ev.events = EPOLLIN;
    ev.data.fd = sock;
    if(epoll_ctl(epollfd, EPOLL_CTL_ADD, sock, &ev) == -1) {
        perror("epoll_ctl() sock");
        close(epollfd);
        close(sock);
        free(buffer);
        return 1;
    }

    for(int i = 0; i != workers; ++i) {
        if(fork() != 0) {
            break;
        }
    }
    pid_t pid = getpid();
    printf("Worker process %i started\n", pid);

    while(1) {
        struct sockaddr client_addr;
        socklen_t client_addr_len = INET_ADDRSTRLEN;

        nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        if(nfds == -1) {
            perror("epoll_wait() error");
            close(epollfd);
            close(sock);
            free(buffer);
            return 1;
        }
        for(int i = 0; i != nfds; ++i) {
            if(events[i].data.fd == sock) {
                int client_socket = accept(sock, &client_addr, &client_addr_len);
                if(client_socket == -1) {
                    continue;
                }

                int flags = fcntl(client_socket, F_GETFL, 0);
                if(flags == -1) {
                    perror("fcntl(..., F_GETFL, ...) error");
                    continue;
                }
                flags |= O_NONBLOCK;
                if(fcntl(client_socket, F_SETFL, flags) == -1) {
                    perror("fcntl(..., F_SETFL, ...) error");
                    continue;
                }

                // ev.events = EPOLLIN | EPOLLET;
                ev.events = EPOLLIN;
                ev.data.fd = client_socket;
                if (epoll_ctl(epollfd, EPOLL_CTL_ADD, client_socket, &ev) == -1) {
                    perror("epoll_ctl(..., EPOLL_CTL_ADD, ...) error");
                    continue;
                }
            }
            else {
                int recvd = recv(events[i].data.fd, buffer, RECV_BUFFER_SIZE, 0);
                if(recvd > 0) {
                    if(verbose) {
                        struct in_addr ipAddr = ((struct sockaddr_in*)&client_addr)->sin_addr;
                        inet_ntop(AF_INET, &ipAddr, str, INET_ADDRSTRLEN);

                        struct timeval te;
                        gettimeofday(&te, NULL);
                        struct tm tm = *localtime(&te.tv_sec);

                        printf("%i> [%04i-%02i-%02i %02i:%02i:%02i] %s ", pid, tm.tm_year + 1900, tm.tm_mon, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, str);
                    }

                    int ret = http_request(buffer, recvd, events[i].data.fd);
                    if(ret < 0 && verbose) {
                        printf("http_request() returned %i ", ret);
                    }

                    if(verbose) {
                        putchar('\n');
                    }
                }
                else if(recvd == 0) {
                    epoll_ctl(epollfd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
                    close(events[i].data.fd);
                }
            }
        }
    }

    close(sock);
    free(buffer);
    close(epollfd);

    printf("%s has stoped\n", SERVER_NAME);
    return 0;
}
