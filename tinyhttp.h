// Copyright (C) 2024 Aleksei Rogov <alekzzzr@gmail.com>. All rights reserved.

#define SERVER_NAME "tinyhttp"

#define RECV_BUFFER_SIZE (4096)
#define SEND_BUFFER_SIZE (4096)
#define FILE_BUFFER_SIZE (1024 << 10)
#define CGI_BUFFER_SIZE  (4096)

#define DEFAULT_PORT  9000
#define MAX_CLIENTS   SOMAXCONN
#define WORKERS       4
#define MAX_EVENTS    200

#define HTTP11_SIGNATURE 0x312e312F50545448

#define RESPONSE_100  0
#define RESPONSE_200  1
#define RESPONSE_400  2
#define RESPONSE_401  3
#define RESPONSE_403  4
#define RESPONSE_404  5
#define RESPONSE_405  6
#define RESPONSE_500  7
#define RESPONSE_501  8
#define RESPONSE_502  9

#define REQUEST_EMPTY                 -1
#define REQUEST_INVALID               -2
#define REQUEST_INVALID_PATH          -3
#define REQUEST_UNSUPPORTED           -4
#define REQUEST_INVALID_HEADERS       -5
#define REQUEST_METHOD_UNSUPPORTED    -6
#define REQUEST_PROTOCOL_UNSUPPORTED  -7

#define CONFIG_NOTFOUND      -1
#define CONFIG_INCORRECT     -2
#define CONFIG_MALLOC_ERROR  -3

#define CGI_PIPE_ERROR  -1
#define CGI_FORK_ERROR  -2
#define CGI_EXEC_ERROR  -3

#define PREDEF_ENV           17
#define FCGI_ROLE            "FCGI_ROLE=RESPONDER"
#define QUERY_STRING         "QUERY_STRING=%s"
#define REQUEST_METHOD_GET   "REQUEST_METHOD=GET"
#define REQUEST_METHOD_POST  "REQUEST_METHOD=POST"
#define CONTENT_TYPE         "CONTENT_TYPE=%s"
#define CONTENT_LENGTH       "CONTENT_LENGTH=%s"
#define SCRIPT_NAME          "SCRIPT_NAME=%s"
#define REQUEST_URI          "REQUEST_URI=%s?%s"
#define GATEWAY_INTERFACE    "GATEWAY_INTERFACE=CGI/1.1"
#define SERVER_SOFTWARE      "SERVER_SOFTWARE="SERVER_NAME
#define REQUEST_SCHEME       "REQUEST_SCHEME=http"
#define SERVER_PROTOCOL      "SERVER_PROTOCOL=HTTP/1.1"
#define DOCUMENT_URI         "DOCUMENT_URI=%s"
#define SCRIPT_FILENAME      "SCRIPT_FILENAME=%s%s"
#define DOCUMENT_ROOT        "DOCUMENT_ROOT=%s"
#define REMOTE_ADDR          "REMOTE_ADDR=%s"
#define REMOTE_PORT          "REMOTE_PORT=%i"
#define SERVER_ADDR          "SERVER_ADDR=%s"
#define SERVER_PORT          "SERVER_PORT=%i"
#define SERVER_HOST          "SERVER_NAME=%s"

typedef struct {
    char name[10];
    uint8_t len;
    uint8_t key;
} http_method_t;

enum METHODS {
    GET,
    POST,
    HEAD,
    OPTIONS,
    PUT,
    DELETE
};

http_method_t http_methods[] = {
    {"GET ", 4, GET},
    {"POST ", 5, POST},
    {"HEAD ", 5, HEAD},
    {"OPTIONS ", 8, OPTIONS},
    {"PUT ", 4, PUT},
    {"DELETE ", 7, DELETE}
};

typedef struct {
    uint8_t method;
    char *path;
    char *query;
    char *version;
} request_t;

typedef struct {
    char *msg;
    int msg_len;
    int code;
} responses_t;

responses_t responses[] = {
    {"100 Continue", 12, 100},
    {"200 OK", 6, 200},
    {"400 Bad Request", 15, 400},
    {"401 Unauthorized", 16, 401},
    {"403 Forbidden", 13, 403},
    {"404 Not Found", 13, 404},
    {"405 Method Not Allowed", 22, 405},
    {"500 Internal Server Error", 25, 500},
    {"501 Not Implemented", 19, 501},
    {"502 Bad Gateway", 15, 502}
};

struct CONFIG_PATH {
    char *content_type;
    char *action;
};
