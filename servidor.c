#include <stdio.h>              // Para funciones de entrada/salida estándar
#include <stdlib.h>             // Para funciones como exit()
#include <string.h>             // Para manejo de cadenas
#include <unistd.h>             // Para funciones POSIX como close()
#include <netinet/in.h>         // Para estructuras y constantes de sockets

#define PORT 5000               // Puerto donde escucha el servidor
#define BUFFER_SIZE 1024        // Tamaño del buffer de lectura/escritura

// Función que valida que la clave no tenga caracteres peligrosos o inválidos
int clave_valida(const char *clave) {
    if (strlen(clave) == 0) return 0;
    for (int i = 0; clave[i]; i++) {
        if (clave[i] == '/' || clave[i] == '\\' || clave[i] == '.' || clave[i] == ' ') {
            return 0; // Clave inválida
        }
    }
    return 1; // Clave aceptable
}

// Función que maneja los comandos enviados por el cliente
void handle_command(int client_fd) {
    char buffer[BUFFER_SIZE] = {0};
    char response[BUFFER_SIZE] = {0};

    // Lee el comando enviado por el cliente (hasta BUFFER_SIZE bytes)
    ssize_t bytes_read = read(client_fd, buffer, BUFFER_SIZE - 1);
    if (bytes_read <= 0) {
        perror("Error al leer del cliente");
        close(client_fd);
        exit(EXIT_FAILURE);
    }

    char cmd[10], key[100], value[BUFFER_SIZE];
    int matched = sscanf(buffer, "%s %s %[^\n]", cmd, key, value);  // Leer comando, clave y valor (si existe)

    if (!clave_valida(key)) {
        snprintf(response, BUFFER_SIZE, "ERROR: Clave inválida\n");
        write(client_fd, response, strlen(response));
        close(client_fd);
        return;
    }

    // Comando SET: crea archivo <key> y guarda <value>
    if (strcmp(cmd, "SET") == 0 && matched == 3) {
        FILE *fp = fopen(key, "w");
        if (!fp) {
            perror("No se pudo crear el archivo");
            exit(EXIT_FAILURE);
        }
        fprintf(fp, "%s", value);
        fclose(fp);
        snprintf(response, BUFFER_SIZE, "OK\n");
    }
    // Comando GET: lee archivo <key> y responde su contenido
    else if (strcmp(cmd, "GET") == 0 && matched >= 2) {
        FILE *fp = fopen(key, "r");
        if (!fp) {
            snprintf(response, BUFFER_SIZE, "NOTFOUND\n");
        } else {
            char contenido[BUFFER_SIZE];
            fread(contenido, 1, BUFFER_SIZE - 1, fp);
            contenido[BUFFER_SIZE - 1] = '\0';
            fclose(fp);
            snprintf(response, BUFFER_SIZE, "OK\n%s\n", contenido);
        }
    }
    // Comando DEL: elimina el archivo si existe
    else if (strcmp(cmd, "DEL") == 0 && matched >= 2) {
        remove(key); // No importa si falla, igual respondemos OK
        snprintf(response, BUFFER_SIZE, "OK\n");
    }
    // Comando inválido
    else {
        snprintf(response, BUFFER_SIZE, "ERROR: Comando inválido\n");
    }

    // Enviar respuesta al cliente y cerrar conexión
    write(client_fd, response, strlen(response));
    close(client_fd);
}

// Función principal: inicializa el socket y espera conexiones
int main() {
    int server_fd, client_fd;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);
    int opt = 1;

    // Crear socket TCP
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("No se pudo crear el socket");
        exit(EXIT_FAILURE);
    }

    // Permitir reutilización de dirección/puerto
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Error en setsockopt");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Configurar dirección del servidor (IPv4, cualquier interfaz, puerto 5000)
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // Asociar socket a la dirección configurada
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Error en bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Escuchar conexiones entrantes (máximo 3 en espera)
    if (listen(server_fd, 3) < 0) {
        perror("Error en listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Servidor clave-valor escuchando en el puerto %d...\n", PORT);

    // Bucle principal: aceptar y manejar una conexión por vez
    while (1) {
        client_fd = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (client_fd < 0) {
            perror("Error en accept");
            continue;
        }
        handle_command(client_fd);
    }

    close(server_fd);
    return 0;
}
