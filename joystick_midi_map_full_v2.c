/* joystick_midi_map_full_v2.c
 *
 * Este programa mejora la lectura del buffer de un dispositivo HID.
 * Cuando se detecta una nueva combinación, se muestra la lectura completa
 * en formato hexadecimal y se le asigna un botón (sin límite de número).
 *
 * Además se permite ingresar texto en el terminal (como título o nota)
 * mientras se hacen las lecturas. Al finalizar (al pulsar "exit"/"q" o con Ctrl+C),
 * se exporta automáticamente un archivo de log ("joystick_log.txt")
 * con el resumen completo de lo mostrado en el terminal, manteniendo el orden.
 *
 * Para compilar en macOS (suponiendo rutas de Homebrew):
 *     gcc joystick_midi_map_full_v2.c -I/opt/homebrew/include -L/opt/homebrew/lib -lhidapi -o joystick_midi_map_full_v2
 */

#pragma clang system_header

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/select.h>
#ifdef __APPLE__
#include "/opt/homebrew/include/hidapi/hidapi.h"
#include "/opt/homebrew/include/portmidi.h"
#else
#include <hidapi/hidapi.h>
#include <portmidi.h>
#endif

#define BUF_SIZE 65
#define NOTE_BUF_SIZE 256

// Estructura para almacenar una combinación única de bytes del reporte HID
typedef struct {
    size_t len;
    unsigned char data[BUF_SIZE];
} ButtonCombo;

ButtonCombo* combos = NULL;
int combo_count = 0;
int combo_capacity = 0;

// --- Nuevo: Log global de eventos ---
char **event_log = NULL;
int event_count = 0;
int event_capacity = 0;

void record_event(const char *event) {
    if (event_count >= event_capacity) {
        event_capacity = (event_capacity == 0) ? 100 : event_capacity * 2;
        char **tmp = realloc(event_log, event_capacity * sizeof(char *));
        if (!tmp) {
            fprintf(stderr, "Error al asignar memoria para el log de eventos.\n");
            exit(1);
        }
        event_log = tmp;
    }
    event_log[event_count] = strdup(event);
    event_count++;
}
// --- Fin log global de eventos ---

// Función para buscar una combinación ya registrada
int find_combo(const unsigned char *buf, size_t len) {
    for (int i = 0; i < combo_count; i++) {
        if (combos[i].len == len && memcmp(combos[i].data, buf, len) == 0) {
            return i;
        }
    }
    return -1;
}

// Almacena una nueva combinación (realloc dinámico)
void store_combo(const unsigned char *buf, size_t len) {
    if (combo_count >= combo_capacity) {
        combo_capacity = (combo_capacity == 0) ? 100 : combo_capacity * 2;
        ButtonCombo *tmp = realloc(combos, combo_capacity * sizeof(ButtonCombo));
        if (!tmp) {
            fprintf(stderr, "Error al asignar memoria para combos.\n");
            exit(1);
        }
        combos = tmp;
    }
    combos[combo_count].len = len;
    memcpy(combos[combo_count].data, buf, len);
    combo_count++;
}

// Función para imprimir un buffer en formato hexadecimal a un stream
void print_buffer(const unsigned char *buf, size_t len, FILE *stream) {
    for (size_t i = 0; i < len; i++) {
        fprintf(stream, "%02X ", buf[i]);
    }
}

// Función para exportar el log completo al archivo "joystick_log.txt"
void export_event_log() {
    FILE *fp = fopen("joystick_log.txt", "w");
    if (fp == NULL) {
        fprintf(stderr, "Error al abrir el archivo de log para exportar.\n");
        return;
    }
    for (int i = 0; i < event_count; i++) {
        fprintf(fp, "%s\n", event_log[i]);
    }
    fclose(fp);
    printf("Log exportado a 'joystick_log.txt'\n");
}

// Manejador de señales para capturar Ctrl+C y exportar el log automáticamente
void sigint_handler(int signum) {
    export_event_log();
    exit(0);
}

int main() {
    int res;
    unsigned char buf[BUF_SIZE];
    hid_device *handle;

    // Registrar el manejador SIGINT para detectar Ctrl+C
    signal(SIGINT, sigint_handler);

    if (hid_init()) {
        fprintf(stderr, "Error al inicializar hidapi\n");
        return -1;
    }

    // Abrir el dispositivo HID usando Vendor ID y Product ID fijos
    unsigned short vendor_id = 0x1949;
    unsigned short product_id = 0x0402;
    handle = hid_open(vendor_id, product_id, NULL);
    if (!handle) {
        fprintf(stderr, "No se pudo abrir el joystick\n");
        hid_exit();
        return -1;
    }
    // Establecer modo no bloqueante para que la lectura HID no bloquee la entrada de teclado
    hid_set_nonblocking(handle, 1);

    printf("Logger de entradas HID iniciado.\n");
    printf("Presione botones en el controlador para registrar entradas.\n");
    printf("Escriba texto (título/nota) en el terminal y presione Enter para agregarla.\n");
    printf("Escriba 'exit' o 'q' para terminar el programa.\n");

    // Registrar en log el inicio
    record_event("Logger de entradas HID iniciado.");
    
    // Bucle principal: leer continuamente los datos HID y comprobar entrada de teclado
    while (1) {
        // Intentar leer datos del dispositivo HID
        res = hid_read(handle, buf, BUF_SIZE);
        if (res < 0) {
            fprintf(stderr, "Error al leer del joystick\n");
            break;
        }
        if (res > 0) {
            // Si es nueva combinación, la registramos y mostramos la lectura completa
            int idx = find_combo(buf, res);
            if (idx == -1) {
                idx = combo_count;
                store_combo(buf, res);
                char event_line[512];
                int pos = snprintf(event_line, sizeof(event_line), "Nueva combinación detectada - Botón %d: ", idx);
                for (size_t i = 0; i < res; i++) {
                    pos += snprintf(event_line + pos, sizeof(event_line) - pos, "%02X ", buf[i]);
                }
                record_event(event_line);
                printf("%s\n", event_line);
            }
        }

        // Comprobar si hay entrada de texto en STDIN (sin bloquear)
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        struct timeval tv = {0, 10000}; // 10 ms de espera
        int sel = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
        if (sel > 0 && FD_ISSET(STDIN_FILENO, &fds)) {
            char note_buf[NOTE_BUF_SIZE];
            if (fgets(note_buf, sizeof(note_buf), stdin) != NULL) {
                // Eliminar salto de línea al final
                size_t len = strlen(note_buf);
                if (len > 0 && note_buf[len - 1] == '\n') {
                    note_buf[len - 1] = '\0';
                }
                if (strcmp(note_buf, "exit") == 0 || strcmp(note_buf, "q") == 0) {
                    record_event("Comando de salida recibido. Terminando...");
                    printf("Comando de salida recibido. Terminando...\n");
                    break;
                } else {
                    char note_event[NOTE_BUF_SIZE + 10];
                    snprintf(note_event, sizeof(note_event), "Nota: %s", note_buf);
                    record_event(note_event);
                    printf("%s\n", note_event);
                }
            }
        }

        usleep(1000); // Pausa de 1 ms para evitar uso intensivo de CPU
    }

    // Al salir, exportar el log completo
    export_event_log();

    hid_close(handle);
    hid_exit();
    return 0;
} 