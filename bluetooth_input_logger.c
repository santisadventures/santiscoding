/* bluetooth_input_logger.c
 *
 * Este programa lee continuamente el buffer de un dispositivo HID (por ejemplo, un controlador bluetooth)
 * y asigna cada bit (a partir del índice BUTTON_START_INDEX) a un "botón". Además, permite al usuario
 * escribir entradas de texto en el terminal. Todas las interacciones (tanto las del HID como las del usuario)
 * se registran en el archivo "bluetooth_log.txt" en el directorio del proyecto.
 *
 * Para compilar:
 *     gcc bluetooth_input_logger.c -lhidapi -o bluetooth_input_logger
 *
 * Asegúrese de tener instaladas las bibliotecas hidapi y las dependencias necesarias.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <hidapi/hidapi.h>

#define BUFFER_SIZE         65
#define BUTTON_START_INDEX  5
#define MAX_BUTTONS         512  // Capacidad máxima de botones a mapear
#define MAX_INPUT_SIZE      256  // Tamaño máximo de entrada
#define VENDOR_ID 0x1234  // Reemplaza con el ID de tu dispositivo
#define PRODUCT_ID 0x5678 // Reemplaza con el ID de tu dispositivo

// Función para registrar la entrada en el archivo de log
void log_input(FILE *logFile, const char *input) {
    fprintf(logFile, "Entrada: %s\n", input);
    fflush(logFile);
}

// Función para mostrar la entrada en formato hexadecimal
void print_hex(const unsigned char *buf, size_t len) {
    printf("Entrada en hexadecimal: ");
    for (size_t i = 0; i < len; i++) {
        printf("%02X ", buf[i]);
    }
    printf("\n");
}

void guardar_datos(const char *datos) {
    FILE *archivo = fopen("registro_actividades.txt", "a");
    if (archivo == NULL) {
        perror("Error al abrir el archivo");
        return;
    }
    fprintf(archivo, "%s\n", datos);
    fclose(archivo);
}

int main(void) {
    int res;
    unsigned char buf[BUFFER_SIZE];
    hid_device *handle = NULL;

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

    // Configurar el dispositivo en modo no bloqueante para poder leer sin quedar bloqueado
    if (hid_set_nonblocking(handle, 1) < 0) {
        fprintf(stderr, "Error al establecer modo no bloqueante\n");
        hid_close(handle);
        hid_exit();
        return -1;
    }

    // Abrir archivo de log para escribir en el directorio del proyecto
    FILE *logFile = fopen("bluetooth_log.txt", "w");
    if (!logFile) {
        fprintf(stderr, "No se pudo abrir el archivo de log\n");
        hid_close(handle);
        hid_exit();
        return -1;
    }

    printf("Logger iniciado.\n");
    printf("Escriba texto y presione Enter para registrar entradas.\n");
    printf("Escriba 'exit' o 'q' para terminar el programa.\n");

    // Array estático para almacenar el estado previo de cada botón
    static int prev_state[MAX_BUTTONS] = {0};

    int running = 1;
    while (running) {
        /* Leer datos del dispositivo HID (modo no bloqueante) */
        res = hid_read(handle, buf, BUFFER_SIZE);
        if (res < 0) {
            fprintf(stderr, "Error al leer datos del dispositivo HID\n");
        } else if (res > BUTTON_START_INDEX) {
            int total_buttons = (res - BUTTON_START_INDEX) * 8;
            for (size_t i = BUTTON_START_INDEX; i < (size_t)res; i++) {
                for (int bit = 0; bit < 8; bit++) {
                    int button_index = (int)((i - BUTTON_START_INDEX) * 8 + bit);
                    if (button_index >= total_buttons || button_index >= MAX_BUTTONS)
                        break;
                    int current = (buf[i] >> bit) & 0x01;
                    if (current != prev_state[button_index]) {
                        if (current) {
                            // Botón presionado
                            printf("HID: Botón %d presionado\n", button_index);
                            fprintf(logFile, "HID: Botón %d presionado\n", button_index);
                        } else {
                            // Botón liberado
                            printf("HID: Botón %d liberado\n", button_index);
                            fprintf(logFile, "HID: Botón %d liberado\n", button_index);
                        }
                        fflush(logFile);
                        prev_state[button_index] = current;
                    }
                }
            }
        }

        /* Verificar si hay entrada de texto desde el terminal usando select() */
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 10000;  // 10 ms de espera
        int ret = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
        if (ret > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
            char input[MAX_INPUT_SIZE];
            if (fgets(input, sizeof(input), stdin) != NULL) {
                // Eliminar el salto de línea, si existe
                size_t len = strlen(input);
                if (len > 0 && input[len - 1] == '\n') {
                    input[len - 1] = '\0';
                }
                // Si se recibe el comando de salida, terminar el programa
                if (strcmp(input, "exit") == 0 || strcmp(input, "q") == 0) {
                    printf("Comando de salida recibido. Terminando...\n");
                    fprintf(logFile, "USER: Comando de salida recibido. Terminando...\n");
                    running = 0;
                } else {
                    printf("USER: %s\n", input);
                    fprintf(logFile, "USER: %s\n", input);
                }
                fflush(logFile);
            }
        }

        // Pequeña pausa para reducir el uso intensivo de la CPU
        usleep(10000);  // 10 ms
    }

    // Cerrar recursos y terminar
    fclose(logFile);
    hid_close(handle);
    hid_exit();
    printf("Log finalizado. Archivo 'bluetooth_log.txt' generado en el directorio del proyecto.\n");

    while (1) {
        // Leer datos del dispositivo
        res = hid_read(handle, buf, sizeof(buf));
        if (res > 0) {
            // Convertir los datos a una cadena
            char datos[64];
            snprintf(datos, sizeof(datos), "Datos: ");
            for (int i = 0; i < res; i++) {
                snprintf(datos + strlen(datos), sizeof(datos) - strlen(datos), "%02X ", buf[i]);
            }
            // Guardar los datos en el archivo
            guardar_datos(datos);
            printf("%s\n", datos); // Imprimir en la consola
        }
        usleep(100000); // Esperar 100 ms
    }

    return 0;
}