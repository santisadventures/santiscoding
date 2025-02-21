/* input_buffer_logger.c
 *
 * Este programa lee continuamente el buffer de un dispositivo HID y muestra los inputs en el terminal.
 * Registra todos los datos en un archivo de log.
 *
 * Para compilar:
 *     gcc input_buffer_logger.c -lhidapi -o input_buffer_logger
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <hidapi/hidapi.h>

#define BUFFER_SIZE         65
#define VENDOR_ID 0x1949    // Reemplaza con el ID de tu dispositivo
#define PRODUCT_ID 0x0402   // Reemplaza con el ID de tu dispositivo

// Función para registrar la entrada en el archivo de log
void log_input(FILE *logFile, const char *input) {
    fprintf(logFile, "%s\n", input);
    fflush(logFile);
}

// Función para mostrar los datos en el formato requerido
void print_data(const unsigned char *buf, size_t len) {
    printf("Botón: ");
    for (size_t i = 0; i < len; i++) {
        printf("%02X ", buf[i]);
    }
    printf("\n");
}

int main(void) {
    int res;
    unsigned char buf[BUFFER_SIZE];
    hid_device *handle;
    FILE *logFile = fopen("input_log.txt", "w");
    if (!logFile) {
        fprintf(stderr, "No se pudo abrir el archivo de log\n");
        return -1;
    }

    // Inicializar hidapi
    if (hid_init() != 0) {
        fprintf(stderr, "Error al inicializar hidapi\n");
        return -1;
    }

    // Abrir el dispositivo HID
    handle = hid_open(VENDOR_ID, PRODUCT_ID, NULL);
    if (!handle) {
        fprintf(stderr, "No se pudo abrir el dispositivo HID\n");
        hid_exit();
        return -1;
    }

    printf("Logger de entradas HID iniciado.\n");
    printf("Presione botones en el controlador para registrar entradas.\n");
    printf("Escriba 'exit' o 'q' para terminar el programa.\n");

    while (1) {
        // Leer datos del dispositivo HID
        res = hid_read(handle, buf, sizeof(buf));
        if (res < 0) {
            fprintf(stderr, "Error al leer del dispositivo HID\n");
            break;
        } else if (res > 0) {
            // Mostrar los datos en el formato requerido
            print_data(buf, (size_t)res);
            // Registrar los datos en el log
            log_input(logFile, "Datos: ");
            for (size_t i = 0; i < res; i++) {
                fprintf(logFile, "%02X ", buf[i]);
            }
            fprintf(logFile, "\n");
        }

        // Comprobar si el usuario quiere salir
        char input[10];
        if (fgets(input, sizeof(input), stdin) != NULL) {
            // Eliminar el salto de línea, si existe
            size_t len = strlen(input);
            if (len > 0 && input[len - 1] == '\n') {
                input[len - 1] = '\0';
            }

            // Si se recibe el comando de salida, terminar el programa
            if (strcmp(input, "exit") == 0 || strcmp(input, "q") == 0) {
                printf("Comando de salida recibido. Terminando...\n");
                log_input(logFile, "Comando de salida recibido. Terminando...");
                break;
            }
        }

        usleep(10000); // Pausa de 10 ms para evitar uso intensivo de CPU
    }

    // Cerrar recursos y terminar
    fclose(logFile);
    hid_close(handle);
    hid_exit();
    printf("Log finalizado. Archivo 'input_log.txt' generado en el directorio del proyecto.\n");
    return 0;
}