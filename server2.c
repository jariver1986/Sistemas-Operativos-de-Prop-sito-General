// servidor2.c — Servidor TCP clave-valor (SET/GET/DEL)
// - Funciones pequeñas (parseo, validación, handlers, envío)
// - Usa bool (stdbool.h)
// - Sin warnings con -Wall -Wextra -pedantic
// - Cierre ordenado con SIGINT
// - Sección one-shot al final (comentada) para Valgrind

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <signal.h>
#include <errno.h>

#define PORT 5000
#define BUFFER_SIZE 1024

typedef enum {
    CMD_INVALID = 0,
    CMD_SET,
    CMD_GET,
    CMD_DEL
} Command;

typedef struct {
    Command cmd;
    char key[100];                 // máx 99 + '\0'
    char value[BUFFER_SIZE];       // usado en SET
} Request;

static volatile sig_atomic_t g_stop = 0;
static void on_sigint(int sig) { (void)sig; g_stop = 1; }

// ---------- utilidades ----------
static bool clave_valida(const char *clave) {
    if (clave == NULL || clave[0] == '\0') return false;
    for (size_t i = 0; clave[i] != '\0'; ++i) {
        char c = clave[i];
        if (c == '/' || c == '\\' || c == '.' || c == ' ') return false;
    }
    return true;
}

static ssize_t write_all(int fd, const void *buf, size_t len) {
    const char *p = (const char*)buf;
    size_t n = len;
    while (n > 0) {
        ssize_t w = write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += (size_t)w;
        n -= (size_t)w;
    }
    return (ssize_t)len;
}

// ---------- parseo ----------
static Command parse_cmd(const char *cmd_str) {
    if (strcmp(cmd_str, "SET") == 0) return CMD_SET;
    if (strcmp(cmd_str, "GET") == 0) return CMD_GET;
    if (strcmp(cmd_str, "DEL") == 0) return CMD_DEL;
    return CMD_INVALID;
}

// 0 ok; <0 error de formato/parámetros
static int parse_request(const char *buffer, Request *req) {
    if (!buffer || !req) return -1;

    char cmd_str[10] = {0};
    memset(req, 0, sizeof *req);

    // Formatos:
    //   SET <key> <value...>
    //   GET <key>
    //   DEL <key>
    int matched = sscanf(buffer, "%9s %99s %1023[^\n]", cmd_str, req->key, req->value);
    if (matched < 1) return -2; // sin comando

    req->cmd = parse_cmd(cmd_str);
    if (req->cmd == CMD_INVALID) return -3;

    if ((req->cmd == CMD_GET || req->cmd == CMD_DEL) && matched < 2) return -4; // falta clave
    if (req->cmd == CMD_SET && matched < 3) return -5;                          // falta valor

    return 0;
}

// ---------- handlers ----------
static void handle_set(const Request *req, char *response, size_t cap) {
    if (!clave_valida(req->key)) {
        (void)snprintf(response, cap, "ERROR: Clave invalida\n");
        return;
    }
    FILE *fp = fopen(req->key, "w");
    if (!fp) {
        (void)snprintf(response, cap, "ERROR: No se pudo crear\n");
        return;
    }
    fputs(req->value, fp);
    fclose(fp);
    (void)snprintf(response, cap, "OK\n");
}

static void handle_get(const Request *req, char *response, size_t cap) {
    if (!clave_valida(req->key)) {
        (void)snprintf(response, cap, "ERROR: Clave invalida\n");
        return;
    }
    FILE *fp = fopen(req->key, "r");
    if (!fp) {
        (void)snprintf(response, cap, "NOTFOUND\n");
        return;
    }
    char contenido[BUFFER_SIZE];
    size_t n = fread(contenido, 1, BUFFER_SIZE - 1, fp);
    contenido[n] = '\0';
    fclose(fp);

    // "OK\n" + contenido + "\n" => limitar explícitamente el %s
    (void)snprintf(response, cap, "OK\n%.*s\n", (int)(BUFFER_SIZE - 5), contenido);
}

static void handle_del(const Request *req, char *response, size_t cap) {
    if (!clave_valida(req->key)) {
        (void)snprintf(response, cap, "ERROR: Clave invalida\n");
        return;
    }
    (void)remove(req->key); // ignorar resultado
    (void)snprintf(response, cap, "OK\n");
}

// ---------- orquestador por cliente ----------
static void run_request(int client_fd, const Request *req) {
    char response[BUFFER_SIZE] = {0};
    switch (req->cmd) {
        case CMD_SET: handle_set(req, response, sizeof response); break;
        case CMD_GET: handle_get(req, response, sizeof response); break;
        case CMD_DEL: handle_del(req, response, sizeof response); break;
        default: (void)snprintf(response, sizeof response, "ERROR: Comando invalido\n"); break;
    }
    (void)write_all(client_fd, response, strlen(response));
}

// Atiende a un cliente: lee, parsea, ejecuta, responde.
static void handle_client(int client_fd) {
    char buffer[BUFFER_SIZE] = {0};

    ssize_t bytes = read(client_fd, buffer, BUFFER_SIZE - 1);
    if (bytes <= 0) {
        perror("read");
        close(client_fd);
        return;
    }
    buffer[(size_t)bytes] = '\0';

    Request req;
    int st = parse_request(buffer, &req);
    if (st != 0) {
        const char *msg =
            (st == -2 || st == -3) ? "ERROR: Comando invalido\n" :
            (st == -4)             ? "ERROR: Falta clave\n" :
            (st == -5)             ? "ERROR: Falta valor\n" :
                                      "ERROR: Formato invalido\n";
        (void)write_all(client_fd, msg, strlen(msg));
        close(client_fd);
        return;
    }

    run_request(client_fd, &req);
    close(client_fd);
}

// ---------- main ----------
int main(void) {
    // stdout sin buffer: ayuda a Valgrind a no reportar "still reachable" por stdio
    setvbuf(stdout, NULL, _IONBF, 0);
    signal(SIGINT, on_sigint);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return EXIT_FAILURE; }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(server_fd);
        return EXIT_FAILURE;
    }

    struct sockaddr_in address;
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
        close(server_fd);
        return EXIT_FAILURE;
    }

    if (listen(server_fd, 8) < 0) {
        perror("listen");
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("Servidor clave-valor escuchando en el puerto %d...\n", PORT);

    socklen_t addrlen = (socklen_t)sizeof(address);
    while (!g_stop) {
        int client_fd = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (client_fd < 0) {
            if (errno == EINTR && g_stop) break; // interrupción por SIGINT
            perror("accept");
            continue;
        }
        handle_client(client_fd);
    }

    close(server_fd);
    printf("Cerrando servidor ordenadamente.\n");
    return 0;

    // ===== ONE-SHOT (comentado) — pruebas Valgrind =====
    // // Acepta UNA conexión y termina limpio:
    // int client_fd = accept(server_fd, (struct sockaddr *)&address, &addrlen);
    // if (client_fd < 0) { perror("accept"); close(server_fd); return EXIT_FAILURE; }
    // handle_client(client_fd);
    // close(server_fd);
    // return 0;
}
