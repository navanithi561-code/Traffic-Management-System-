#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include "api.h"

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef SOCKET sock_t;
  #define CLOSESOCK closesocket
  static long long now_ms(void) { return (long long)GetTickCount64(); }
#else
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <sys/select.h>
  #include <sys/time.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <time.h>
  typedef int sock_t;
  #define INVALID_SOCKET (-1)
  #define CLOSESOCK close
  static long long now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000; }
#endif

#define MAX_REQ 16384
static volatile int g_run = 1;
static void on_sigint(int sig) { (void)sig; g_run = 0; }

static const char *reason(int c) {
    switch (c) { case 200: return "OK"; case 201: return "Created"; case 204: return "No Content";
        case 400: return "Bad Request"; case 404: return "Not Found"; case 405: return "Method Not Allowed";
        case 409: return "Conflict"; case 413: return "Payload Too Large"; default: return "Internal Server Error"; }
}

static void send_all(sock_t c, const char *p, size_t n) {
    while (n > 0) {
        int k = (int)send(c, p, (int)(n > 8192 ? 8192 : n), 0);
        if (k <= 0) return;
        p += k; n -= (size_t)k;
    }
}

static void respond(sock_t c, int code, const char *ctype, const char *body, size_t len, const char *cors) {
    char h[640];
    int n = snprintf(h, sizeof h, "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nCache-Control: no-store\r\nConnection: close\r\n",
                     code, reason(code), ctype, len);
    if (cors) n += snprintf(h + n, sizeof h - (size_t)n, "Access-Control-Allow-Origin: %.100s\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type\r\n", cors);
    snprintf(h + n, sizeof h - (size_t)n, "\r\n");
    send_all(c, h, strlen(h));
    if (len) send_all(c, body, len);
}
static void respond_err(sock_t c, int code, const char *msg, const char *cors) {
    char b[160]; int n = snprintf(b, sizeof b, "{\"ok\":false,\"error\":\"%s\"}", msg);
    respond(c, code, "application/json", b, (size_t)n, cors);
}

/* Case-insensitive search for "Content-Length:" at the start of a header line. */
static int content_length(const char *req, int hdr_end) {
    static const char key[] = "content-length:";
    for (int i = 0; i < hdr_end; i++) {
        if (i != 0 && req[i - 1] != '\n') continue;
        int k = 0;
        while (key[k] && i + k < hdr_end && tolower((unsigned char)req[i + k]) == key[k]) k++;
        if (!key[k]) { long v = strtol(req + i + k, NULL, 10); return (v < 0 || v > MAX_REQ) ? -1 : (int)v; }
    }
    return 0;
}

static void serve_page(sock_t c, const char *page, const char *cors) {
    FILE *f = fopen(page, "rb");
    if (!f) { respond_err(c, 404, "frontend file not found - run the server from the backend folder or use --page", cors); return; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = (n >= 0 && n < 4000000) ? malloc((size_t)n + 1) : NULL;
    if (!buf) { fclose(f); respond_err(c, 500, "cannot load page", cors); return; }
    size_t got = fread(buf, 1, (size_t)n, f); fclose(f);
    respond(c, 200, "text/html; charset=utf-8", buf, got, cors);
    free(buf);
}

static void handle_client(sock_t c, Sim *s, Graph *g, const char *cors, const char *page) {
    static char req[MAX_REQ + 1];
    int len = 0, hdr_end = -1, clen = 0;
    while (len < MAX_REQ) {
        int n = (int)recv(c, req + len, MAX_REQ - len, 0);
        if (n <= 0) break;
        len += n; req[len] = 0;
        if (hdr_end < 0) { char *p = strstr(req, "\r\n\r\n"); if (p) { hdr_end = (int)(p - req) + 4; clen = content_length(req, hdr_end); } }
        if (hdr_end >= 0) {
            if (clen < 0 || hdr_end + clen > MAX_REQ) { respond_err(c, 413, "request too large", cors); return; }
            if (len >= hdr_end + clen) break;
        }
    }
    if (hdr_end < 0 || len < hdr_end + clen) { respond_err(c, 400, "incomplete or malformed request", cors); return; }

    char method[8], target[512];
    if (sscanf(req, "%7s %511s", method, target) != 2) { respond_err(c, 400, "malformed request line", cors); return; }
    char *query = strchr(target, '?');
    if (query) *query++ = 0; else query = (char *)"";

    if (strcmp(method, "OPTIONS") == 0 && cors) { respond(c, 204, "text/plain", "", 0, cors); return; }
    if (strcmp(method, "GET") == 0 && (strcmp(target, "/") == 0 || strcmp(target, "/index.html") == 0)) { serve_page(c, page, cors); return; }
    if (strncmp(target, "/api/", 5) != 0) { respond_err(c, 404, "not found", cors); return; }

    Buf out; buf_init(&out);
    int code = api_handle(s, g, method, target, query, req + hdr_end, now_ms(), &out);
    respond(c, code, "application/json", out.d ? out.d : "{}", out.d ? out.len : 2, cors);
    buf_free(&out);
}

int main(int argc, char **argv) {
    int port = 8080;
    const char *cors = NULL, *page = "../frontend/index.html";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--cors") && i + 1 < argc) cors = argv[++i];
        else if (!strcmp(argv[i], "--page") && i + 1 < argc) page = argv[++i];
        else { printf("Usage: %s [--port N] [--cors ORIGIN] [--page path/to/index.html]\n", argv[0]); return 1; }
    }
    if (port < 1 || port > 65535) { fprintf(stderr, "invalid port\n"); return 1; }
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { fprintf(stderr, "WSAStartup failed\n"); return 1; }
#endif
    Sim sim; Graph graph;
    sim_init(&sim); graph_build_demo(&graph);

    sock_t ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls == INVALID_SOCKET) { fprintf(stderr, "socket() failed\n"); return 1; }
    int yes = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes);
    struct sockaddr_in addr; memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET; addr.sin_port = htons((unsigned short)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);          /* local only: not exposed to the network */
    if (bind(ls, (struct sockaddr *)&addr, sizeof addr) < 0 || listen(ls, 16) < 0) {
        fprintf(stderr, "Cannot bind to port %d (already in use?). Try --port 8081\n", port); return 1;
    }
    signal(SIGINT, on_sigint);
    printf("Traffic Management Simulator running at http://localhost:%d  (Ctrl+C to stop)\n", port);

    while (g_run) {
        fd_set fds; FD_ZERO(&fds); FD_SET(ls, &fds);
        struct timeval tv = {0, 100000};
        int r = select((int)ls + 1, &fds, NULL, NULL, &tv);
        sim_advance(&sim, now_ms());
        if (r > 0 && FD_ISSET(ls, &fds)) {
            sock_t c = accept(ls, NULL, NULL);
            if (c == INVALID_SOCKET) continue;
#ifdef _WIN32
            DWORD to = 3000; setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char *)&to, sizeof to);
#else
            struct timeval to = {3, 0}; setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof to);
#endif
            handle_client(c, &sim, &graph, cors, page);
            CLOSESOCK(c);
        }
    }
    CLOSESOCK(ls);
    sim_free(&sim); graph_free(&graph);
#ifdef _WIN32
    WSACleanup();
#endif
    printf("\nServer stopped cleanly.\n");
    return 0;
}
