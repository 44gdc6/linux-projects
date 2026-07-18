#define _GNU_SOURCE

#include "http/http_server.h"

#include "ingest/payload_parser.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/epoll.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define MAX_EVENTS 64
#define REQ_BUF_SIZE 65536
#define RESP_BUF_SIZE 131072
#define MAX_HEADER_SIZE 8192
#define MAX_BODY_SIZE 49152
#define SENDFILE_CHUNK (1024 * 1024)

typedef struct http_request {
    char method[8];
    char path[512];
    char query[512];
    char authorization[256];
    char device_token[256];
    char *body;
    int body_len;
    int content_length;
} http_request_t;

static const char *g_cors_origin = "*";

static int request_content_length(const char *buf, int *content_length);
static int request_has_header(const char *buf, const char *name);

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static const char *mime_type(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (ext == NULL) {
        return "application/octet-stream";
    }
    if (strcmp(ext, ".html") == 0) return "text/html; charset=utf-8";
    if (strcmp(ext, ".css") == 0) return "text/css; charset=utf-8";
    if (strcmp(ext, ".js") == 0) return "application/javascript; charset=utf-8";
    if (strcmp(ext, ".json") == 0) return "application/json; charset=utf-8";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    return "application/octet-stream";
}

static int send_all(int fd, const char *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        int flags = 0;
        ssize_t n;
#ifdef MSG_NOSIGNAL
        flags = MSG_NOSIGNAL;
#endif
        n = send(fd, buf + sent, len - sent, flags);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

static void send_response(int fd, int status, const char *status_text,
                          const char *content_type, const char *body,
                          int include_body, const char *extra_headers)
{
    char header[512];
    size_t body_len = body ? strlen(body) : 0;
    int n = snprintf(header, sizeof(header),
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %zu\r\n"
                     "Access-Control-Allow-Origin: %s\r\n"
                     "Access-Control-Allow-Methods: GET, HEAD, POST, PUT, OPTIONS\r\n"
                     "Access-Control-Allow-Headers: Content-Type, Authorization, X-Device-Token\r\n"
                     "X-Content-Type-Options: nosniff\r\n"
                     "%s"
                     "Connection: close\r\n\r\n",
                     status, status_text, content_type, body_len,
                     g_cors_origin && g_cors_origin[0] ? g_cors_origin : "*",
                     extra_headers ? extra_headers : "");
    if (n < 0 || (size_t)n >= sizeof(header)) {
        return;
    }
    if (send_all(fd, header, (size_t)n) == 0 && include_body && body_len > 0) {
        send_all(fd, body, body_len);
    }
}

static void send_text(int fd, int status, const char *status_text,
                      const char *content_type, const char *body)
{
    send_response(fd, status, status_text, content_type, body, 1, NULL);
}

static void send_options(int fd, const char *allow)
{
    char extra[128];
    int n = snprintf(extra, sizeof(extra),
                     "Allow: %s\r\n"
                     "Access-Control-Max-Age: 600\r\n",
                     allow);
    if (n < 0 || (size_t)n >= sizeof(extra)) {
        return;
    }
    send_response(fd, 204, "No Content", "text/plain; charset=utf-8", "", 0, extra);
}

static void send_method_not_allowed(int fd, const char *allow)
{
    char extra[128];
    int n = snprintf(extra, sizeof(extra), "Allow: %s\r\n", allow);
    if (n < 0 || (size_t)n >= sizeof(extra)) {
        return;
    }
    send_response(fd, 405, "Method Not Allowed", "application/json; charset=utf-8",
                  "{\"error\":\"method not allowed\"}", 1, extra);
}

static const char *query_param(const char *query, const char *name, char *out, size_t out_size)
{
    const char *segment;
    size_t name_len;

    if (query == NULL || name == NULL || out == NULL || out_size == 0) {
        return NULL;
    }
    name_len = strlen(name);
    segment = query;
    while (*segment != '\0') {
        const char *end = strchr(segment, '&');
        size_t segment_len = end ? (size_t)(end - segment) : strlen(segment);

        if (segment_len > name_len && memcmp(segment, name, name_len) == 0 &&
            segment[name_len] == '=') {
            size_t value_len = segment_len - name_len - 1;
            if (value_len >= out_size) {
                return NULL;
            }
            memcpy(out, segment + name_len + 1, value_len);
            out[value_len] = '\0';
            return out;
        }
        if (end == NULL) {
            break;
        }
        segment = end + 1;
    }
    return NULL;
}

static int parse_limit(const char *text, int fallback, int max)
{
    long value;
    char *end = NULL;

    if (text == NULL || *text == '\0') {
        return fallback;
    }
    errno = 0;
    if (!isdigit((unsigned char)*text)) {
        return fallback;
    }
    value = strtol(text, &end, 10);
    if (text == end || *end != '\0' || errno == ERANGE || value <= 0 || value > max) {
        return fallback;
    }
    return (int)value;
}

static int parse_u64_param(const char *text, uint64_t *out)
{
    unsigned long long value;
    char *end = NULL;

    if (out == NULL || text == NULL || *text == '\0' ||
        *text == '-' || *text == '+') {
        return -1;
    }
    errno = 0;
    value = strtoull(text, &end, 10);
    if (text == end || *end != '\0' || errno == ERANGE) {
        return -1;
    }
    *out = (uint64_t)value;
    return 0;
}

static int request_header_value(const char *buf, const char *name,
                                char *out, size_t out_size)
{
    const char *headers_end;
    const char *line;
    size_t name_len;

    if (buf == NULL || name == NULL || out == NULL || out_size == 0) {
        return 0;
    }
    out[0] = '\0';

    headers_end = strstr(buf, "\r\n\r\n");
    if (headers_end == NULL) {
        return 0;
    }
    line = strstr(buf, "\r\n");
    if (line == NULL) {
        return 0;
    }
    line += 2;
    name_len = strlen(name);

    while (line < headers_end) {
        const char *line_end = strstr(line, "\r\n");
        const char *colon;
        const char *value_start;
        const char *value_end;
        size_t value_len;

        if (line_end == NULL || line_end > headers_end) {
            return 0;
        }
        colon = memchr(line, ':', (size_t)(line_end - line));
        if (colon == NULL ||
            (size_t)(colon - line) != name_len ||
            strncasecmp(line, name, name_len) != 0) {
            line = line_end + 2;
            continue;
        }

        value_start = colon + 1;
        while (value_start < line_end && isspace((unsigned char)*value_start)) {
            value_start++;
        }
        value_end = line_end;
        while (value_end > value_start && isspace((unsigned char)*(value_end - 1))) {
            value_end--;
        }
        value_len = (size_t)(value_end - value_start);
        if (value_len >= out_size) {
            return 0;
        }
        memcpy(out, value_start, value_len);
        out[value_len] = '\0';
        return 1;
    }
    return 0;
}

static int token_matches(const char *provided, const char *expected)
{
    return provided != NULL && expected != NULL && expected[0] != '\0' &&
           strcmp(provided, expected) == 0;
}

static int request_authorized(const app_context_t *ctx, const http_request_t *req)
{
    const char *prefix = "Bearer ";
    size_t prefix_len = strlen(prefix);

    if (ctx == NULL || ctx->auth_token == NULL || ctx->auth_token[0] == '\0') {
        return 1;
    }
    if (req == NULL) {
        return 0;
    }
    if (token_matches(req->device_token, ctx->auth_token)) {
        return 1;
    }
    if (strncasecmp(req->authorization, prefix, prefix_len) == 0 &&
        token_matches(req->authorization + prefix_len, ctx->auth_token)) {
        return 1;
    }
    return 0;
}

static void send_unauthorized(int fd)
{
    send_response(fd, 401, "Unauthorized", "application/json; charset=utf-8",
                  "{\"error\":\"unauthorized\"}", 1,
                  "WWW-Authenticate: Bearer realm=\"Sensor Monitor\"\r\n");
}

static void json_escape_string(const char *input, char *output, size_t output_size)
{
    size_t len = 0;

    if (output == NULL || output_size == 0) {
        return;
    }
    if (input == NULL) {
        input = "";
    }

    for (; *input != '\0' && len + 1 < output_size; input++) {
        unsigned char ch = (unsigned char)*input;
        const char *escaped = NULL;
        char unicode_escape[7];

        switch (ch) {
        case '"': escaped = "\\\""; break;
        case '\\': escaped = "\\\\"; break;
        case '\b': escaped = "\\b"; break;
        case '\f': escaped = "\\f"; break;
        case '\n': escaped = "\\n"; break;
        case '\r': escaped = "\\r"; break;
        case '\t': escaped = "\\t"; break;
        default:
            if (ch < 0x20) {
                snprintf(unicode_escape, sizeof(unicode_escape), "\\u%04x", ch);
                escaped = unicode_escape;
            }
            break;
        }

        if (escaped != NULL) {
            size_t escaped_len = strlen(escaped);
            if (len + escaped_len >= output_size) {
                break;
            }
            memcpy(output + len, escaped, escaped_len);
            len += escaped_len;
        } else {
            output[len++] = (char)ch;
        }
    }
    output[len] = '\0';
}

static int is_known_method(const char *method)
{
    return strcmp(method, "GET") == 0 ||
           strcmp(method, "HEAD") == 0 ||
           strcmp(method, "POST") == 0 ||
           strcmp(method, "PUT") == 0 ||
           strcmp(method, "DELETE") == 0 ||
           strcmp(method, "OPTIONS") == 0;
}

static int has_ctl_char(const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    while (*p != '\0') {
        if (*p < 0x20 || *p == 0x7f) {
            return 1;
        }
        p++;
    }
    return 0;
}

static int header_value_is_safe(const char *text)
{
    const unsigned char *p = (const unsigned char *)text;

    if (text == NULL || text[0] == '\0') {
        return 0;
    }
    while (*p != '\0') {
        if (*p == '\r' || *p == '\n' || *p < 0x20 || *p == 0x7f) {
            return 0;
        }
        p++;
    }
    return 1;
}

static int parse_request(char *buf, int len, http_request_t *req)
{
    char *line_end;
    char *body_start;
    char url[1024];
    char method[16];
    char version[16];
    char extra;
    char *qmark;
    int content_length = 0;

    if (buf == NULL || req == NULL || len <= 0) {
        return -1;
    }
    memset(req, 0, sizeof(*req));
    buf[len] = '\0';

    body_start = strstr(buf, "\r\n\r\n");
    if (request_content_length(buf, &content_length) != 0) {
        return -1;
    }
    (void)request_header_value(buf, "Authorization",
                               req->authorization, sizeof(req->authorization));
    (void)request_header_value(buf, "X-Device-Token",
                               req->device_token, sizeof(req->device_token));

    line_end = strstr(buf, "\r\n");
    if (line_end == NULL) {
        return -1;
    }
    *line_end = '\0';
    if (sscanf(buf, "%15s %1023s %15s %c", method, url, version, &extra) != 3) {
        return -1;
    }
    if (strlen(method) >= sizeof(req->method) || !is_known_method(method)) {
        return -1;
    }
    if (strcmp(version, "HTTP/1.1") != 0 && strcmp(version, "HTTP/1.0") != 0) {
        return -1;
    }
    if (url[0] != '/' || has_ctl_char(url) || strchr(url, '#') != NULL) {
        return -1;
    }
    snprintf(req->method, sizeof(req->method), "%s", method);

    qmark = strchr(url, '?');
    if (qmark != NULL) {
        *qmark = '\0';
        if (strlen(qmark + 1) >= sizeof(req->query)) {
            return -1;
        }
        snprintf(req->query, sizeof(req->query), "%s", qmark + 1);
    }
    if (strlen(url) >= sizeof(req->path)) {
        return -1;
    }
    snprintf(req->path, sizeof(req->path), "%s", url);

    req->content_length = content_length;
    if (body_start != NULL && content_length > 0) {
        body_start += 4;
        if (body_start + content_length > buf + len) {
            return -1;
        }
        req->body = body_start;
        req->body_len = content_length;
        req->body[req->body_len] = '\0';
    }
    return 0;
}

static int request_content_length(const char *buf, int *content_length)
{
    const char *headers_end;
    const char *line;
    const char *name = "Content-Length";
    size_t name_len = strlen(name);
    int seen = 0;

    if (buf == NULL || content_length == NULL) {
        return -1;
    }

    *content_length = 0;
    headers_end = strstr(buf, "\r\n\r\n");
    if (headers_end == NULL) {
        return 0;
    }

    line = strstr(buf, "\r\n");
    if (line == NULL) {
        return 0;
    }
    line += 2;

    while (line < headers_end) {
        const char *line_end = strstr(line, "\r\n");
        const char *colon;
        const char *value_start;
        char *value_end = NULL;
        long value;

        if (line_end == NULL || line_end > headers_end) {
            return -1;
        }
        colon = memchr(line, ':', (size_t)(line_end - line));
        if (colon == NULL) {
            line = line_end + 2;
            continue;
        }
        if ((size_t)(colon - line) != name_len ||
            strncasecmp(line, name, name_len) != 0) {
            line = line_end + 2;
            continue;
        }
        if (seen) {
            return -1;
        }
        seen = 1;

        value_start = colon + 1;
        while (value_start < line_end && isspace((unsigned char)*value_start)) {
            value_start++;
        }
        if (value_start >= line_end || !isdigit((unsigned char)*value_start)) {
            return -1;
        }
        errno = 0;
        value = strtol(value_start, &value_end, 10);
        while (value_end < line_end && isspace((unsigned char)*value_end)) {
            value_end++;
        }
        if (value_start == value_end || value_end != line_end ||
            errno == ERANGE || value < 0 || value > INT_MAX) {
            return -1;
        }

        *content_length = (int)value;
        line = line_end + 2;
    }

    return 0;
}

static int request_has_header(const char *buf, const char *name)
{
    const char *headers_end;
    const char *line;
    size_t name_len;

    if (buf == NULL || name == NULL) {
        return 0;
    }

    headers_end = strstr(buf, "\r\n\r\n");
    if (headers_end == NULL) {
        return 0;
    }
    line = strstr(buf, "\r\n");
    if (line == NULL) {
        return 0;
    }
    line += 2;
    name_len = strlen(name);

    while (line < headers_end) {
        const char *line_end = strstr(line, "\r\n");
        const char *colon;

        if (line_end == NULL || line_end > headers_end) {
            return 0;
        }
        colon = memchr(line, ':', (size_t)(line_end - line));
        if (colon != NULL &&
            (size_t)(colon - line) == name_len &&
            strncasecmp(line, name, name_len) == 0) {
            return 1;
        }
        line = line_end + 2;
    }
    return 0;
}

static int is_safe_url_path(const char *url_path)
{
    const unsigned char *p = (const unsigned char *)url_path;

    if (url_path == NULL || url_path[0] != '/') {
        return 0;
    }
    if (strstr(url_path, "..") != NULL) {
        return 0;
    }
    while (*p != '\0') {
        if (*p < 0x20 || *p == 0x7f || *p == '\\') {
            return 0;
        }
        p++;
    }
    return 1;
}

static int path_is_under_root(const char *root, const char *path)
{
    size_t root_len = strlen(root);

    if (strcmp(root, "/") == 0) {
        return 0;
    }
    return strncmp(root, path, root_len) == 0 &&
           (path[root_len] == '\0' || path[root_len] == '/');
}

static void serve_static(app_context_t *ctx, int fd, const char *url_path, int head_only)
{
    char path[1024];
    char root_real[PATH_MAX];
    char file_real[PATH_MAX];
    char header[512];
    struct stat st;
    int file_fd;
    const char *rel = url_path;
    int n;

    if (!is_safe_url_path(url_path)) {
        send_text(fd, 403, "Forbidden", "text/plain; charset=utf-8", "forbidden");
        return;
    }
    if (strcmp(rel, "/") == 0) {
        rel = "/index.html";
    }
    n = snprintf(path, sizeof(path), "%s%s", ctx->www_root, rel);
    if (n < 0 || (size_t)n >= sizeof(path)) {
        send_text(fd, 414, "URI Too Long", "text/plain; charset=utf-8", "uri too long");
        return;
    }
    if (realpath(ctx->www_root, root_real) == NULL) {
        send_text(fd, 500, "Internal Server Error", "text/plain; charset=utf-8", "invalid www root");
        return;
    }
    if (realpath(path, file_real) == NULL) {
        send_text(fd, 404, "Not Found", "text/plain; charset=utf-8", "not found");
        return;
    }
    if (!path_is_under_root(root_real, file_real)) {
        send_text(fd, 403, "Forbidden", "text/plain; charset=utf-8", "forbidden");
        return;
    }

    file_fd = open(file_real, O_RDONLY | O_CLOEXEC);
    if (file_fd < 0 || fstat(file_fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        if (file_fd >= 0) close(file_fd);
        send_text(fd, 404, "Not Found", "text/plain; charset=utf-8", "not found");
        return;
    }

    n = snprintf(header, sizeof(header),
                 "HTTP/1.1 200 OK\r\n"
                 "Content-Type: %s\r\n"
                 "Content-Length: %lld\r\n"
                 "Access-Control-Allow-Origin: %s\r\n"
                 "Access-Control-Allow-Methods: GET, HEAD, POST, PUT, OPTIONS\r\n"
                 "Access-Control-Allow-Headers: Content-Type, Authorization, X-Device-Token\r\n"
                 "X-Content-Type-Options: nosniff\r\n"
                 "Connection: close\r\n\r\n",
                 mime_type(file_real), (long long)st.st_size,
                 g_cors_origin && g_cors_origin[0] ? g_cors_origin : "*");
    if (n < 0 || (size_t)n >= sizeof(header) ||
        send_all(fd, header, (size_t)n) != 0 || head_only) {
        close(file_fd);
        return;
    }
    off_t offset = 0;
    while (offset < st.st_size) {
        off_t remaining = st.st_size - offset;
        size_t chunk = remaining > SENDFILE_CHUNK ? SENDFILE_CHUNK : (size_t)remaining;
        ssize_t sent = sendfile(fd, file_fd, &offset, chunk);
        if (sent <= 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
    }
    close(file_fd);
}

static int build_status_json(app_context_t *ctx, char *buffer, size_t buffer_size)
{
    device_sample_t sample;
    char config_json[512];
    char bind_host[128];
    char www_root[1024];
    char db_path[1024];
    char config_path[1024];
    char cors_origin[512];
    char device[256];
    const char *cors = g_cors_origin && g_cors_origin[0] ? g_cors_origin : "*";
    int has_sample;
    int n;

    if (ctx == NULL || buffer == NULL || buffer_size == 0) {
        return -1;
    }
    if (web_config_to_json(&ctx->config, config_json, sizeof(config_json)) != 0) {
        return -1;
    }

    has_sample = realtime_cache_copy(ctx->cache, &sample) == 0;
    if (!has_sample) {
        memset(&sample, 0, sizeof(sample));
    }

    json_escape_string(ctx->bind_host ? ctx->bind_host : "", bind_host, sizeof(bind_host));
    json_escape_string(ctx->www_root ? ctx->www_root : "", www_root, sizeof(www_root));
    json_escape_string(ctx->db_path ? ctx->db_path : "", db_path, sizeof(db_path));
    json_escape_string(ctx->config_path ? ctx->config_path : "", config_path, sizeof(config_path));
    json_escape_string(cors, cors_origin, sizeof(cors_origin));
    json_escape_string(sample.device, device, sizeof(device));

    n = snprintf(buffer, buffer_size,
                 "{"
                 "\"status\":\"ok\","
                 "\"server_time\":%llu,"
                 "\"bind\":\"%s\","
                 "\"port\":%d,"
                 "\"paths\":{\"www_root\":\"%s\",\"db\":\"%s\",\"config\":\"%s\"},"
                 "\"auth\":{\"enabled\":%s},"
                 "\"cors\":{\"origin\":\"%s\"},"
                 "\"telemetry\":{\"has_sample\":%s,\"device\":\"%s\",\"timestamp\":%llu},"
                 "\"config\":%s"
                 "}",
                 (unsigned long long)time(NULL),
                 bind_host,
                 ctx->port,
                 www_root,
                 db_path,
                 config_path,
                 ctx->auth_token && ctx->auth_token[0] ? "true" : "false",
                 cors_origin,
                 has_sample ? "true" : "false",
                 has_sample ? device : "",
                 has_sample ? (unsigned long long)sample.timestamp : 0ULL,
                 config_json);
    if (n < 0 || (size_t)n >= buffer_size) {
        return -1;
    }
    return 0;
}

static void handle_api(app_context_t *ctx, int fd, http_request_t *req)
{
    char json[RESP_BUF_SIZE];
    char sensor[64];
    char limit_buf[32];
    char start_buf[32];
    char end_buf[32];
    uint64_t start_time = 0;
    uint64_t end_time = 0;
    int limit = 100;

    if (strcmp(req->path, "/api/config") == 0) {
        if (strcmp(req->method, "GET") == 0) {
            if (web_config_to_json(&ctx->config, json, sizeof(json)) != 0) {
                send_text(fd, 500, "Internal Server Error", "application/json; charset=utf-8",
                          "{\"error\":\"config failed\"}");
                return;
            }
            send_text(fd, 200, "OK", "application/json; charset=utf-8", json);
            return;
        }
        if (strcmp(req->method, "PUT") == 0) {
            web_config_t next = ctx->config;

            if (req->body == NULL || req->body_len <= 0 ||
                web_config_apply_json(&next, req->body) != 0) {
                send_text(fd, 400, "Bad Request", "application/json; charset=utf-8",
                          "{\"error\":\"invalid config\"}");
                return;
            }
            if (web_config_save(&next, ctx->config_path) != 0) {
                send_text(fd, 500, "Internal Server Error", "application/json; charset=utf-8",
                          "{\"error\":\"config save failed\"}");
                return;
            }
            ctx->config = next;
            if (web_config_to_json(&ctx->config, json, sizeof(json)) != 0) {
                send_text(fd, 500, "Internal Server Error", "application/json; charset=utf-8",
                          "{\"error\":\"config failed\"}");
                return;
            }
            send_text(fd, 200, "OK", "application/json; charset=utf-8", json);
            return;
        }
        send_method_not_allowed(fd, "GET, PUT, OPTIONS");
        return;
    }
    if (strcmp(req->method, "GET") != 0) {
        send_method_not_allowed(fd, "GET, OPTIONS");
        return;
    }
    if (strcmp(req->path, "/api/status") == 0) {
        if (build_status_json(ctx, json, sizeof(json)) != 0) {
            send_text(fd, 500, "Internal Server Error", "application/json; charset=utf-8",
                      "{\"error\":\"status failed\"}");
            return;
        }
        send_text(fd, 200, "OK", "application/json; charset=utf-8", json);
        return;
    }
    if (strcmp(req->path, "/api/realtime") == 0) {
        realtime_cache_to_json(ctx->cache, json, sizeof(json));
        send_text(fd, 200, "OK", "application/json; charset=utf-8", json);
        return;
    }
    if (strcmp(req->path, "/api/history") == 0) {
        if (query_param(req->query, "limit", limit_buf, sizeof(limit_buf)) != NULL) {
            limit = parse_limit(limit_buf, 100, 1000);
        }
        if (query_param(req->query, "start", start_buf, sizeof(start_buf)) != NULL &&
            parse_u64_param(start_buf, &start_time) != 0) {
            send_text(fd, 400, "Bad Request", "application/json; charset=utf-8",
                      "{\"error\":\"invalid start\"}");
            return;
        }
        if (query_param(req->query, "end", end_buf, sizeof(end_buf)) != NULL &&
            parse_u64_param(end_buf, &end_time) != 0) {
            send_text(fd, 400, "Bad Request", "application/json; charset=utf-8",
                      "{\"error\":\"invalid end\"}");
            return;
        }
        if (query_param(req->query, "sensor", sensor, sizeof(sensor)) == NULL) {
            snprintf(sensor, sizeof(sensor), "temperature");
        }
        if (storage_history_range_json(ctx->storage, sensor, start_time, end_time,
                                       limit, json, sizeof(json)) != 0) {
            send_text(fd, 500, "Internal Server Error", "application/json", "{\"error\":\"history failed\"}");
            return;
        }
        send_text(fd, 200, "OK", "application/json; charset=utf-8", json);
        return;
    }
    if (strcmp(req->path, "/api/alarms") == 0) {
        if (query_param(req->query, "limit", limit_buf, sizeof(limit_buf)) != NULL) {
            limit = parse_limit(limit_buf, 50, 500);
        }
        if (storage_alarms_json(ctx->storage, limit, json, sizeof(json)) != 0) {
            send_text(fd, 500, "Internal Server Error", "application/json", "{\"error\":\"alarms failed\"}");
            return;
        }
        send_text(fd, 200, "OK", "application/json; charset=utf-8", json);
        return;
    }
    if (strcmp(req->path, "/api/export") == 0) {
        if (query_param(req->query, "limit", limit_buf, sizeof(limit_buf)) != NULL) {
            limit = parse_limit(limit_buf, 1000, 2000);
        } else {
            limit = 1000;
        }
        if (query_param(req->query, "start", start_buf, sizeof(start_buf)) != NULL &&
            parse_u64_param(start_buf, &start_time) != 0) {
            send_text(fd, 400, "Bad Request", "application/json; charset=utf-8",
                      "{\"error\":\"invalid start\"}");
            return;
        }
        if (query_param(req->query, "end", end_buf, sizeof(end_buf)) != NULL &&
            parse_u64_param(end_buf, &end_time) != 0) {
            send_text(fd, 400, "Bad Request", "application/json; charset=utf-8",
                      "{\"error\":\"invalid end\"}");
            return;
        }
        if (query_param(req->query, "sensor", sensor, sizeof(sensor)) == NULL) {
            sensor[0] = '\0';
        }
        if (storage_export_csv(ctx->storage, sensor, start_time, end_time,
                               limit, json, sizeof(json)) != 0) {
            send_text(fd, 500, "Internal Server Error", "application/json; charset=utf-8",
                      "{\"error\":\"export failed\"}");
            return;
        }
        send_response(fd, 200, "OK", "text/csv; charset=utf-8", json, 1,
                      "Content-Disposition: attachment; filename=\"sensor-history.csv\"\r\n");
        return;
    }
    send_text(fd, 404, "Not Found", "application/json", "{\"error\":\"api not found\"}");
}

static void handle_ingest(app_context_t *ctx, int fd, http_request_t *req)
{
    device_sample_t sample;

    if (strcmp(req->method, "POST") != 0) {
        send_method_not_allowed(fd, "POST, OPTIONS");
        return;
    }
    if (req->body == NULL || req->body_len <= 0) {
        send_text(fd, 400, "Bad Request", "application/json", "{\"error\":\"empty body\"}");
        return;
    }
    req->body[req->body_len] = '\0';
    if (payload_parse_json(req->body, &sample) != 0) {
        send_text(fd, 400, "Bad Request", "application/json", "{\"error\":\"invalid json\"}");
        return;
    }
    if (storage_insert_sample(ctx->storage, &sample) != 0) {
        send_text(fd, 500, "Internal Server Error", "application/json", "{\"error\":\"storage failed\"}");
        return;
    }
    realtime_cache_update(ctx->cache, &sample);
    send_text(fd, 200, "OK", "application/json", "{\"status\":\"ok\"}");
}

static void handle_options(int fd, const http_request_t *req)
{
    if (strcmp(req->path, "/api/config") == 0) {
        send_options(fd, "GET, PUT, OPTIONS");
    } else if (strncmp(req->path, "/api/", 5) == 0) {
        send_options(fd, "GET, OPTIONS");
    } else if (strcmp(req->path, "/ingest/telemetry") == 0 ||
               strcmp(req->path, "/ingest/onenet") == 0) {
        send_options(fd, "POST, OPTIONS");
    } else {
        send_options(fd, "GET, HEAD, OPTIONS");
    }
}

static void handle_client(app_context_t *ctx, int fd)
{
    char *buf = calloc(1, REQ_BUF_SIZE + 1);
    int total = 0;
    int read_status = 0;
    http_request_t req;

    if (buf == NULL) {
        close(fd);
        return;
    }

    while (total < REQ_BUF_SIZE) {
        ssize_t n = recv(fd, buf + total, REQ_BUF_SIZE - total, 0);
        if (n > 0) {
            char *headers_end;
            int content_length;
            int header_bytes;

            total += (int)n;
            buf[total] = '\0';
            headers_end = strstr(buf, "\r\n\r\n");
            if (headers_end == NULL) {
                if (total >= MAX_HEADER_SIZE) {
                    read_status = 431;
                    break;
                }
                continue;
            }
            header_bytes = (int)(headers_end + 4 - buf);
            if (header_bytes > MAX_HEADER_SIZE) {
                read_status = 431;
                break;
            }
            if (request_has_header(buf, "Transfer-Encoding")) {
                read_status = 400;
                break;
            }
            if (request_content_length(buf, &content_length) != 0) {
                read_status = 400;
                break;
            }
            if (content_length > MAX_BODY_SIZE ||
                content_length > REQ_BUF_SIZE - header_bytes) {
                read_status = 413;
                break;
            }
            if (total >= header_bytes + content_length) {
                break;
            }
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        break;
    }

    if (read_status == 0) {
        char *headers_end = strstr(buf, "\r\n\r\n");
        int content_length = 0;

        if (headers_end == NULL) {
            read_status = total >= MAX_HEADER_SIZE ? 431 : 400;
        } else {
            int header_bytes = (int)(headers_end + 4 - buf);
            if (header_bytes > MAX_HEADER_SIZE) {
                read_status = 431;
            } else if (request_has_header(buf, "Transfer-Encoding")) {
                read_status = 400;
            } else if (request_content_length(buf, &content_length) != 0) {
                read_status = 400;
            } else if (content_length > MAX_BODY_SIZE ||
                       content_length > REQ_BUF_SIZE - header_bytes) {
                read_status = 413;
            } else if (total < header_bytes + content_length) {
                read_status = 400;
            }
        }
    }

    if (read_status == 400) {
        send_text(fd, 400, "Bad Request", "text/plain; charset=utf-8", "bad request");
    } else if (read_status == 413) {
        send_text(fd, 413, "Payload Too Large", "text/plain; charset=utf-8", "payload too large");
    } else if (read_status == 431) {
        send_text(fd, 431, "Request Header Fields Too Large", "text/plain; charset=utf-8", "headers too large");
    } else if (parse_request(buf, total, &req) != 0) {
        send_text(fd, 400, "Bad Request", "text/plain; charset=utf-8", "bad request");
    } else if (strcmp(req.method, "OPTIONS") == 0) {
        handle_options(fd, &req);
    } else if (req.body_len > 0 && strcmp(req.method, "POST") != 0 &&
               !(strcmp(req.method, "PUT") == 0 &&
                 strcmp(req.path, "/api/config") == 0)) {
        send_text(fd, 400, "Bad Request", "text/plain; charset=utf-8", "request body not allowed");
    } else if ((strncmp(req.path, "/api/", 5) == 0 ||
                strcmp(req.path, "/ingest/telemetry") == 0 ||
                strcmp(req.path, "/ingest/onenet") == 0) &&
               !request_authorized(ctx, &req)) {
        send_unauthorized(fd);
    } else if (strncmp(req.path, "/api/", 5) == 0) {
        handle_api(ctx, fd, &req);
    } else if (strcmp(req.path, "/ingest/telemetry") == 0 || strcmp(req.path, "/ingest/onenet") == 0) {
        handle_ingest(ctx, fd, &req);
    } else {
        if (strcmp(req.method, "GET") != 0 && strcmp(req.method, "HEAD") != 0) {
            send_method_not_allowed(fd, "GET, HEAD, OPTIONS");
        } else {
            serve_static(ctx, fd, req.path, strcmp(req.method, "HEAD") == 0);
        }
    }

    free(buf);
    close(fd);
}

int http_server_run(app_context_t *ctx)
{
    int listen_fd;
    int epfd;
    struct sockaddr_in addr;
    struct epoll_event ev;
    struct epoll_event events[MAX_EVENTS];

    if (ctx == NULL || ctx->www_root == NULL || ctx->cache == NULL ||
        ctx->storage == NULL || ctx->port < 1 || ctx->port > 65535) {
        fprintf(stderr, "invalid http server context\n");
        return -1;
    }

    signal(SIGPIPE, SIG_IGN);
    if (ctx->cors_origin != NULL && ctx->cors_origin[0] != '\0') {
        if (!header_value_is_safe(ctx->cors_origin)) {
            fprintf(stderr, "invalid CORS origin\n");
            return -1;
        }
        g_cors_origin = ctx->cors_origin;
    }

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return -1;
    }

    int reuse = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    if (inet_pton(AF_INET,
                  ctx->bind_host && ctx->bind_host[0] ? ctx->bind_host : "127.0.0.1",
                  &addr.sin_addr) != 1) {
        fprintf(stderr, "invalid bind address: %s\n",
                ctx->bind_host ? ctx->bind_host : "");
        close(listen_fd);
        return -1;
    }
    addr.sin_port = htons((uint16_t)ctx->port);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("bind");
        close(listen_fd);
        return -1;
    }
    if (listen(listen_fd, 128) != 0) {
        perror("listen");
        close(listen_fd);
        return -1;
    }
    if (set_nonblocking(listen_fd) != 0) {
        perror("fcntl");
        close(listen_fd);
        return -1;
    }

    epfd = epoll_create1(0);
    if (epfd < 0) {
        perror("epoll_create1");
        close(listen_fd);
        return -1;
    }

    ev.events = EPOLLIN;
    ev.data.fd = listen_fd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev) != 0) {
        perror("epoll_ctl listen");
        close(epfd);
        close(listen_fd);
        return -1;
    }

    printf("webserver listening on %s:%d, root=%s, auth=%s\n",
           ctx->bind_host && ctx->bind_host[0] ? ctx->bind_host : "127.0.0.1",
           ctx->port, ctx->www_root,
           ctx->auth_token && ctx->auth_token[0] ? "enabled" : "disabled");
    while (1) {
        int n = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("epoll_wait");
            break;
        }
        for (int i = 0; i < n; i++) {
            if (events[i].data.fd == listen_fd) {
                while (1) {
                    int client = accept(listen_fd, NULL, NULL);
                    struct timeval timeout;
                    if (client < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break;
                        }
                        perror("accept");
                        break;
                    }
                    timeout.tv_sec = 2;
                    timeout.tv_usec = 0;
                    if (setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0 ||
                        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0) {
                        perror("setsockopt client timeout");
                        close(client);
                        continue;
                    }
                    ev.events = EPOLLIN | EPOLLONESHOT;
                    ev.data.fd = client;
                    if (epoll_ctl(epfd, EPOLL_CTL_ADD, client, &ev) != 0) {
                        perror("epoll_ctl client");
                        close(client);
                    }
                }
            } else {
                int client = events[i].data.fd;
                if (epoll_ctl(epfd, EPOLL_CTL_DEL, client, NULL) != 0) {
                    perror("epoll_ctl del");
                }
                handle_client(ctx, client);
            }
        }
    }

    close(epfd);
    close(listen_fd);
    return 0;
}
