# Sistemas-Operativos-de-Proposito-General
Materia de especialización en Sistemas Embebidos UBA

# Trabajo Práctico: Servidor TCP Clave-Valor

## 1. Instalación de herramientas necesarias (Windows + WSL)

### 1.1. Activar WSL (Windows Subsystem for Linux)

1. Abrir PowerShell como Administrador.
2. Ejecutar:

   ```powershell
   wsl --install
   ```
3. Reiniciar el sistema cuando se solicite.

> Esto instalará Ubuntu por defecto. Si ya tienes WSL instalado, puedes actualizarlo:

```bash
wsl --update
```

### 1.2. Acceder a Ubuntu desde Windows

1. Presionar `Windows + S` y buscar "Ubuntu".
2. Abrirlo y configurar usuario y contraseña.

### 1.3. Instalar compilador y herramientas de red

Dentro de Ubuntu (WSL):

```bash
sudo apt update
sudo apt install build-essential netcat
```

## 2. Teoría de Sockets en Linux

### 2.1. ¿Qué es un socket?

Un **socket** es un punto final de una comunicación entre dos dispositivos. Permite a programas (procesos) intercambiar datos a través de una red o localmente.

### 2.2. Tipos de sockets más comunes

* **SOCK\_STREAM (TCP):** Protocolo orientado a conexión, fiable y ordenado.
* **SOCK\_DGRAM (UDP):** Protocolo sin conexión, rápido pero no garantiza entrega.
* **UNIX sockets:** Para comunicación entre procesos en la misma máquina.

### 2.3. Flujo típico de un servidor TCP

1. `socket()` - Crear un socket.
2. `bind()` - Asociar socket a una dirección IP y puerto.
3. `listen()` - Esperar conexiones.
4. `accept()` - Aceptar una conexión entrante.
5. `read()/write()` - Leer/escribir datos.
6. `close()` - Cerrar la conexión.

---

## 3. Enunciado del Trabajo Práctico

### Objetivo

Implementar un servidor TCP que almacene información ASCII en forma de clave-valor.

### Requisitos

* El servidor debe:

  * Esperar conexiones TCP en el puerto 5000.
  * Leer comandos en ASCII hasta `\n`.
  * Ejecutar el comando y responder.
  * Cerrar la conexión tras cada comando.

### Comandos admitidos:

* `SET <clave> <valor>`

  * Crea un archivo con nombre `<clave>` y contenido `<valor>`.
  * Responde: `OK`

* `GET <clave>`

  * Si el archivo existe, responde:

    ```
    OK
    <contenido>
    ```
  * Si no existe, responde: `NOTFOUND`

* `DEL <clave>`

  * Si existe el archivo, lo elimina.
  * Siempre responde: `OK`

### Casos excepcionales

* Informar el error mediante `perror()` y finalizar con `exit(EXIT_FAILURE)`.

### Cliente

* No es necesario implementar un cliente. Se puede usar `nc` o `telnet`:

```bash
nc localhost 5000
```

---

## 4. Código del servidor TCP comentado

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>

#define PORT 5000
#define BUFFER_SIZE 1024

// Verifica que la clave no tenga caracteres peligrosos
int clave_valida(const char *clave) {
    if (strlen(clave) == 0) return 0;
    for (int i = 0; clave[i]; i++) {
        if (clave[i] == '/' || clave[i] == '\\' || clave[i] == '.' || clave[i] == ' ') {
            return 0;
        }
    }
    return 1;
}

void handle_command(int client_fd) {
    char buffer[BUFFER_SIZE] = {0};
    char response[BUFFER_SIZE] = {0};

    ssize_t bytes_read = read(client_fd, buffer, BUFFER_SIZE - 1);
    if (bytes_read <= 0) {
        perror("Error al leer del cliente");
        close(client_fd);
        exit(EXIT_FAILURE);
    }

    char cmd[10], key[100], value[BUFFER_SIZE];
    int matched = sscanf(buffer, "%s %s %[^"]\n", cmd, key, value);

    if (!clave_valida(key)) {
        snprintf(response, BUFFER_SIZE, "ERROR: Clave invalida\n");
        write(client_fd, response, strlen(response));
        close(client_fd);
        return;
    }

    if (strcmp(cmd, "SET") == 0 && matched == 3) {
        FILE *fp = fopen(key, "w");
        if (!fp) {
            perror("No se pudo crear el archivo");
            exit(EXIT_FAILURE);
        }
        fprintf(fp, "%s", value);
        fclose(fp);
        snprintf(response, BUFFER_SIZE, "OK\n");
    } else if (strcmp(cmd, "GET") == 0 && matched >= 2) {
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
    } else if (strcmp(cmd, "DEL") == 0 && matched >= 2) {
        remove(key);
        snprintf(response, BUFFER_SIZE, "OK\n");
    } else {
        snprintf(response, BUFFER_SIZE, "ERROR: Comando invalido\n");
    }

    write(client_fd, response, strlen(response));
    close(client_fd);
}

int main() {
    int server_fd, client_fd;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);
    int opt = 1;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("No se pudo crear el socket");
        exit(EXIT_FAILURE);
    }

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Error en bind");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0) {
        perror("Error en listen");
        exit(EXIT_FAILURE);
    }

    printf("Servidor clave-valor escuchando en el puerto %d...\n", PORT);

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
```

---

## 5. Conclusiones y recomendaciones

* El uso de sockets TCP permite simular servidores reales de forma sencilla.
* La comunicación ASCII facilita las pruebas con `nc` o `telnet` sin necesidad de cliente dedicado.
* Validar claves evita problemas de seguridad como sobreescritura o path traversal.
* Para entornos más complejos, podría implementarse persistencia en base de datos o almacenamiento binario.

---

### Autor: *Tu nombre aquí*

Este trabajo puede ser subido a GitHub como ejemplo de práctica de redes usando sockets TCP en C.
